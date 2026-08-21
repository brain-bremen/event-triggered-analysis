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
#include "StimulusConfigWindow.h"

#include "../BarMapperNode.h"

#include "TriggerCore/TriggerSource.h"

#include <cmath>

using namespace juce;

namespace EventTriggered
{

namespace
{
    constexpr int rowHeight = 24;
    constexpr int headerHeight = 30;
    constexpr int nameWidth = 70;
    constexpr int patternWidth = 190;
    constexpr int angleWidth = 70;
    constexpr int compassSize = 150;
    constexpr int windowWidth = nameWidth + patternWidth + angleWidth + 40;
} // namespace

// --- Compass ---------------------------------------------------------------

void CompassPreview::setArrows (std::vector<Arrow> arrows)
{
    m_arrows = std::move (arrows);
    repaint();
}

void CompassPreview::paint (Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    const Point<float> centre = bounds.getCentre();
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f - 12.0f;

    g.setColour (Colours::darkgrey);
    g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);

    if (m_arrows.empty())
    {
        g.setColour (Colours::grey);
        g.setFont (FontOptions (11.0f));
        g.drawText ("no angles set", getLocalBounds(), Justification::centred, true);
        return;
    }

    for (const Arrow& arrow : m_arrows)
    {
        const double rad = Rf::degToRad (arrow.canonicalAngleDeg);

        // Screen y grows downwards, visual-field y upwards. Flipped here so the
        // compass agrees with the map; drawn the other way it would show every
        // direction mirrored, which is exactly the class of error it exists to
        // expose.
        const Point<float> tip (centre.x + radius * static_cast<float> (std::cos (rad)),
                                centre.y - radius * static_cast<float> (std::sin (rad)));

        g.setColour (arrow.colour);
        g.drawArrow (Line<float> (centre, tip), 1.5f, 6.0f, 8.0f);

        const Point<float> labelAt (centre.x + (radius + 10.0f) * static_cast<float> (std::cos (rad)),
                                    centre.y - (radius + 10.0f) * static_cast<float> (std::sin (rad)));

        g.setFont (FontOptions (10.0f));
        g.drawText (arrow.label,
                    Rectangle<float> (34.0f, 12.0f).withCentre (labelAt),
                    Justification::centred,
                    false);
    }
}

// --- Window ----------------------------------------------------------------

StimulusConfigWindow::StimulusConfigWindow (BarMapperNode* node,
                                            bool acquisitionIsActive,
                                            Component* anchor)
    : PopupComponent (anchor), m_node (node), m_acquisitionIsActive (acquisitionIsActive)
{
    const auto makeLabel = [this] (const String& text) {
        auto label = std::make_unique<Label> (text, text);
        label->setFont (FontOptions (12.0f));
        addAndMakeVisible (label.get());
        return label;
    };

    m_conventionLabel = makeLabel ("Zero at / turns");

    m_zeroSelector = std::make_unique<ComboBox> ("zero");
    m_zeroSelector->addItemList ({ "Right", "Up", "Left", "Down" }, 1);
    m_zeroSelector->addListener (this);
    addAndMakeVisible (m_zeroSelector.get());

    m_senseSelector = std::make_unique<ComboBox> ("sense");
    m_senseSelector->addItemList ({ "CCW", "CW" }, 1);
    m_senseSelector->addListener (this);
    addAndMakeVisible (m_senseSelector.get());

    m_generateLabel = makeLabel ("Generate");

    m_generateCount = std::make_unique<ComboBox> ("count");
    for (const int n : { 4, 6, 8, 12, 16 })
        m_generateCount->addItem (String (n) + " directions", n);
    m_generateCount->setSelectedId (8, dontSendNotification);
    addAndMakeVisible (m_generateCount.get());

    m_generateButton = std::make_unique<UtilityButton> ("REPLACE");
    m_generateButton->addListener (this);
    addAndMakeVisible (m_generateButton.get());

    m_compass = std::make_unique<CompassPreview>();
    addAndMakeVisible (m_compass.get());

    m_warningLabel = makeLabel ("");
    m_warningLabel->setColour (Label::textColourId, Colours::orange);
    m_warningLabel->setFont (FontOptions (11.0f));

    updatePopup();
}

StimulusConfigWindow::~StimulusConfigWindow() = default;

void StimulusConfigWindow::updatePopup()
{
    if (m_node == nullptr)
        return;

    const Rf::AngleConvention convention = m_node->getAngleConvention();
    m_zeroSelector->setSelectedId (static_cast<int> (convention.zero) + 1, dontSendNotification);
    m_senseSelector->setSelectedId (static_cast<int> (convention.sense) + 1, dontSendNotification);

    rebuildRows();
    refreshCompass();

    setSize (windowWidth,
             headerHeight + rowHeight * (static_cast<int> (m_rows.size()) + 2) + compassSize + 40);
    resized();
}

void StimulusConfigWindow::rebuildRows()
{
    m_rows.clear();

    for (TriggerSource* source : m_node->getTriggerSources().getAll())
    {
        Row row;
        row.source = source;

        row.name = std::make_unique<Label> ("name", source->name);
        row.name->setFont (FontOptions (12.0f));
        row.name->setColour (Label::textColourId, source->colour);
        addAndMakeVisible (row.name.get());

        // Shown read-only. The pattern is the plugin's business, not the user's:
        // it is generated to match VStim's trial-start message and getting it
        // subtly wrong -- a missing trailing boundary, say -- is exactly the
        // failure the generator exists to prevent. Showing it still matters,
        // because "which message arms this row" is the first question when a
        // condition never fires.
        row.armPattern = std::make_unique<Label> ("pattern", source->armPattern.isNotEmpty()
                                                                 ? source->armPattern
                                                                 : String ("(not gated)"));
        row.armPattern->setFont (FontOptions (11.0f));
        row.armPattern->setColour (Label::textColourId, Colours::grey);
        addAndMakeVisible (row.armPattern.get());

        const auto angle = m_node->getSweepAngles().getAngleDeg (source);

        row.angle = std::make_unique<Label> ("angle", angle ? String (*angle, 1) : String());
        row.angle->setEditable (true);
        row.angle->setFont (FontOptions (12.0f));
        row.angle->setColour (Label::backgroundColourId, Colours::black.withAlpha (0.4f));
        row.angle->setColour (Label::textColourId, angle ? Colours::white : Colours::orange);
        row.angle->setTooltip ("Direction of motion in degrees, in the convention above");
        row.angle->addListener (this);
        addAndMakeVisible (row.angle.get());

        m_rows.push_back (std::move (row));
    }

    addAndMakeVisible (m_warningLabel.get());
}

void StimulusConfigWindow::refreshCompass()
{
    const Rf::AngleConvention convention = m_node->getAngleConvention();

    std::vector<CompassPreview::Arrow> arrows;

    for (const Row& row : m_rows)
    {
        const auto angle = m_node->getSweepAngles().getAngleDeg (row.source);

        if (! angle.has_value())
            continue;

        arrows.push_back ({ Rf::toCanonicalDeg (*angle, convention),
                            row.source->name,
                            row.source->colour });
    }

    m_compass->setArrows (std::move (arrows));

    String warnings;
    for (const Rf::AngleSetWarning warning : m_node->checkAngles())
        warnings += (warnings.isEmpty() ? "" : "; ") + String (Rf::describe (warning));

    m_warningLabel->setText (warnings, dontSendNotification);
}

void StimulusConfigWindow::labelTextChanged (Label* label)
{
    for (const Row& row : m_rows)
    {
        if (row.angle.get() != label)
            continue;

        const String text = label->getText().trim();

        if (text.isEmpty())
            break;

        m_node->setAngleForSource (row.source, text.getDoubleValue());
        row.angle->setColour (Label::textColourId, Colours::white);
        break;
    }

    refreshCompass();
}

void StimulusConfigWindow::comboBoxChanged (ComboBox* box)
{
    const auto setParameter = [this] (const char* name, int index) {
        if (auto* parameter = m_node->getParameter (name))
            parameter->setNextValue (index);
    };

    if (box == m_zeroSelector.get())
        setParameter (RfParameterNames::angle_zero, box->getSelectedId() - 1);
    else if (box == m_senseSelector.get())
        setParameter (RfParameterNames::angle_sense, box->getSelectedId() - 1);
    else
        return;

    // The angles in the table do not change; what they *mean* does. Redrawing the
    // compass here is what makes that visible.
    refreshCompass();
}

void StimulusConfigWindow::applyGeneratedDirections()
{
    m_node->generateDirectionSources (m_generateCount->getSelectedId(), 0);
    updatePopup();
}

void StimulusConfigWindow::buttonClicked (Button* button)
{
    if (button != m_generateButton.get())
        return;

    if (m_acquisitionIsActive)
        return;

    // Replaces every existing condition, so it asks first. A generator that
    // appended instead would leave the previous set in place with its own angles
    // and silently mix two stimulus sets into one map -- which is why it replaces,
    // and why replacing has to be deliberate.
    AlertWindow::showOkCancelBox (
        MessageBoxIconType::QuestionIcon,
        "Replace all conditions?",
        "This removes the current trigger sources and their accumulated trials, and "
        "creates "
            + String (m_generateCount->getSelectedId())
            + " evenly spaced directions with arm patterns matching VStim trial types.",
        "Replace",
        "Cancel",
        this,
        ModalCallbackFunction::create ([this] (int result) {
            if (result != 0)
                applyGeneratedDirections();
        }));
}

void StimulusConfigWindow::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (FontOptions (14.0f));
    g.drawText ("Sweep directions", 10, 6, windowWidth - 20, 20, Justification::centredLeft);

    g.setFont (FontOptions (11.0f));
    g.setColour (Colours::grey);
    g.drawText ("Condition        Armed by                                    Angle",
                10,
                headerHeight - 2,
                windowWidth - 20,
                16,
                Justification::centredLeft);
}

void StimulusConfigWindow::resized()
{
    auto bounds = getLocalBounds().reduced (10, 0);
    bounds.removeFromTop (headerHeight + 16);

    for (const Row& row : m_rows)
    {
        auto line = bounds.removeFromTop (rowHeight);
        row.name->setBounds (line.removeFromLeft (nameWidth));
        row.armPattern->setBounds (line.removeFromLeft (patternWidth));
        row.angle->setBounds (line.removeFromLeft (angleWidth).reduced (2));
    }

    bounds.removeFromTop (6);

    auto conventionRow = bounds.removeFromTop (rowHeight);
    m_conventionLabel->setBounds (conventionRow.removeFromLeft (100));
    m_zeroSelector->setBounds (conventionRow.removeFromLeft (70).reduced (2, 1));
    m_senseSelector->setBounds (conventionRow.removeFromLeft (60).reduced (2, 1));

    auto generateRow = bounds.removeFromTop (rowHeight);
    m_generateLabel->setBounds (generateRow.removeFromLeft (60));
    m_generateCount->setBounds (generateRow.removeFromLeft (120).reduced (2, 1));
    m_generateButton->setBounds (generateRow.removeFromLeft (80).reduced (2, 1));

    m_warningLabel->setBounds (bounds.removeFromTop (16));

    m_compass->setBounds (bounds.removeFromTop (compassSize));
}

} // namespace EventTriggered
