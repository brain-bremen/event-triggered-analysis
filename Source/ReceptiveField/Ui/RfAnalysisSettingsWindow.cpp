/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Receptive Field Mapper
    Copyright (C) 2025-2026 Joscha Schmiedt, Universität Bremen

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/
#include "RfAnalysisSettingsWindow.h"

#include "../BarMapperNode.h"
#include "../RfMath/DisplayUnits.h"
#include "SweepDirectionsPanel.h"
#include "TriggerCore/Ui/NestedCallOut.h"

using namespace juce;

namespace EventTriggered
{

RfAnalysisSettingsWindow::RfAnalysisSettingsWindow (BarMapperNode* node,
                                                    bool acquisitionIsActive,
                                                    Component* anchor)
    : PopupComponent (anchor), m_node (node), m_acquisitionIsActive (acquisitionIsActive)
{
    jassert (anchor != nullptr); // PopupComponent dereferences it in its constructor

    if (m_node == nullptr)
        return;

    // The unit selector first, because it changes how every number below is
    // written -- including the ones typed in -- and reading a speed without
    // knowing its unit is worse than not showing it.
    addControl (RfParameterNames::display_unit);
    addControl (RfParameterNames::viewing_distance_mm);
    addControl (RfParameterNames::screen_px_per_mm);
    addSectionBreak();

    // Stimulus geometry first: these describe the experiment, and everything
    // below describes how it is read.
    addControl (RfParameterNames::speed_deg_per_sec);
    addControl (RfParameterNames::sweep_start_deg);
    addControl (RfParameterNames::latency_ms);
    addSectionBreak();

    addControl (RfParameterNames::smoothing_sigma_ms);
    addControl (RfParameterNames::use_absolute_z);
    addControl (RfParameterNames::combine_mode);
    addSectionBreak();

    addControl (RfParameterNames::map_pixels);
    addControl (RfParameterNames::deg_per_pixel);
    addControl (RfParameterNames::map_centre_x);
    addControl (RfParameterNames::map_centre_y);
    addControl (RfParameterNames::border_fraction);

    applyDisplayUnits();

    m_compass = std::make_unique<CompassPreview>();
    addAndMakeVisible (m_compass.get());

    m_directionsButton = std::make_unique<UtilityButton> ("DIRECTIONS...");
    m_directionsButton->setFont (FontOptions (12.0f));
    m_directionsButton->setTooltip (
        "Opens the angle table and the direction generator, one click away rather "
        "than crowding this popup.");
    m_directionsButton->addListener (this);
    addAndMakeVisible (m_directionsButton.get());

    m_warningLabel = std::make_unique<Label> ("warnings", String());
    m_warningLabel->setFont (FontOptions (11.0f));
    m_warningLabel->setColour (Label::textColourId, Colours::orange);
    m_warningLabel->setJustificationType (Justification::topLeft);
    m_warningLabel->setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (m_warningLabel.get());

    refreshCompass();

    setSize (nameWidth + controlWidth + unitWidth + 24,
             headerHeight + rowHeight * static_cast<int> (m_controls.size()) + footerHeight
                 + sectionGap + compassCaptionHeight + compassSize + bottomMargin);
}

RfAnalysisSettingsWindow::~RfAnalysisSettingsWindow() = default;

ParameterControl* RfAnalysisSettingsWindow::addControl (const char* parameterName)
{
    auto* parameter = m_node->getParameter (parameterName);

    if (parameter == nullptr)
        return nullptr;

    auto control = std::make_unique<ParameterControl> (parameter, nameWidth, controlWidth, unitWidth);

    // Deliberately left active during acquisition. See the class comment: these
    // are read-time parameters, and tuning them against a live map is what they
    // are for.
    control->setActive (true);

    // Editing a display parameter has to rewrite every other row in this window,
    // since they are all shown in the unit it selects. The node is told
    // separately, by parameterValueChanged, and repaints the canvas; this hook is
    // only about the popup being self-consistent the moment the value lands.
    if (BarMapperNode::isDisplayParameter (parameterName))
        control->onChange = [this] { applyDisplayUnits(); };

    addAndMakeVisible (control.get());

    ParameterControl* raw = control.get();
    m_controls.push_back (std::move (control));

    return raw;
}

ParameterControl* RfAnalysisSettingsWindow::controlFor (const char* parameterName) const
{
    for (const auto& control : m_controls)
        if (control != nullptr && control->getParameter() != nullptr
            && control->getParameter()->getName() == parameterName)
            return control.get();

    return nullptr;
}

void RfAnalysisSettingsWindow::applyDisplayUnits()
{
    if (m_node == nullptr)
        return;

    const Rf::DisplayUnit unit = m_node->getDisplayUnit();
    const double scale = m_node->getDisplayUnitsPerDegree();
    const String suffix (Rf::unitSuffix (unit));

    // The five fields that carry a linear visual-angle quantity, and the unit
    // each of them is written in. Latency and smoothing are times, the border
    // fraction is a ratio and the map size is a count of map pixels -- none of
    // them changes with the ruler.
    if (auto* control = controlFor (RfParameterNames::speed_deg_per_sec))
        control->setDisplayTransform (scale, suffix + "/s");

    for (const char* name : { RfParameterNames::sweep_start_deg,
                              RfParameterNames::deg_per_pixel,
                              RfParameterNames::map_centre_x,
                              RfParameterNames::map_centre_y })
        if (auto* control = controlFor (name))
            control->setDisplayTransform (scale, suffix);

    // Greyed rather than hidden when the unit is degrees: the rig is still worth
    // documenting, and a row that vanishes takes with it the explanation of what
    // the other two units would mean.
    const bool geometryApplies = unit != Rf::DisplayUnit::Degrees;

    if (auto* control = controlFor (RfParameterNames::viewing_distance_mm))
        control->setActive (geometryApplies);

    // Millimetres need only the distance; the pixel pitch is what turns them
    // into screen pixels.
    if (auto* control = controlFor (RfParameterNames::screen_px_per_mm))
        control->setActive (unit == Rf::DisplayUnit::ScreenPixels);
}

void RfAnalysisSettingsWindow::addSectionBreak()
{
    m_controls.push_back (nullptr);
}

void RfAnalysisSettingsWindow::updatePopup()
{
    // Units before values: the transform decides how each value is written, and
    // setDisplayTransform refreshes the control it is applied to anyway.
    applyDisplayUnits();

    for (auto& control : m_controls)
        if (control != nullptr)
            control->refresh();

    refreshCompass();
    repaint();
}

void RfAnalysisSettingsWindow::refreshCompass()
{
    if (m_node == nullptr)
        return;

    m_compass->setArrows (buildCompassArrows (*m_node));
    m_warningLabel->setText (describeAngleWarnings (*m_node), dontSendNotification);
}

void RfAnalysisSettingsWindow::resized()
{
    int y = headerHeight;

    for (auto& control : m_controls)
    {
        if (control != nullptr)
            control->setBounds (12, y, getWidth() - 24, rowHeight);

        y += rowHeight;
    }

    y += footerHeight + sectionGap + compassCaptionHeight;

    m_compass->setBounds (12, y, compassSize, compassSize);

    const int rightColumnX = 12 + compassSize + 8;
    const int rightColumnWidth = getWidth() - 12 - rightColumnX;

    m_directionsButton->setBounds (rightColumnX, y, rightColumnWidth, directionsButtonHeight);
    m_warningLabel->setBounds (rightColumnX,
                               y + directionsButtonHeight + 4,
                               rightColumnWidth,
                               compassSize - directionsButtonHeight - 4);
}

void RfAnalysisSettingsWindow::buttonClicked (Button* button)
{
    if (button != m_directionsButton.get())
        return;

    auto panel = std::make_unique<SweepDirectionsPanel> (m_node, m_acquisitionIsActive);

    // The popout doesn't own the compass or the warning line -- it just tells
    // this window that something they read might have changed.
    panel->onChanged = [safe = Component::SafePointer<RfAnalysisSettingsWindow> (this)]
    {
        if (safe != nullptr)
            safe->refreshCompass();
    };

    NestedCallOut::show (*button, std::move (panel));
}

void RfAnalysisSettingsWindow::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (12, 0);

    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (FontOptions (14.0f));
    g.drawText ("Mapping settings", header, Justification::centredLeft);

    // Anchored right below the controls rather than to the window's bottom edge:
    // the compass section occupies everything below this note, and that note's
    // claim -- nothing here discards accumulated trials -- is true of the
    // mapping controls above it, not of the direction generator's REPLACE
    // button behind DIRECTIONS... below.
    const int footerY = headerHeight + rowHeight * static_cast<int> (m_controls.size());
    auto footer = Rectangle<int> (12, footerY, getWidth() - 24, footerHeight);
    g.setFont (FontOptions (11.0f));
    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
    g.drawText ("All of these re-read the accumulated trials; none discards them.",
                footer,
                Justification::centredLeft);

    const int captionY = footerY + footerHeight + sectionGap;
    auto caption = Rectangle<int> (12, captionY, getWidth() - 24, compassCaptionHeight);
    g.setFont (FontOptions (12.0f));
    g.setColour (findColour (ThemeColours::defaultText));
    g.drawText ("Sweep directions", caption, Justification::centredLeft);
}

} // namespace EventTriggered
