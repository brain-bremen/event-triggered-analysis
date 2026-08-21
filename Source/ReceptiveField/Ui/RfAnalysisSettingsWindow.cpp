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

    setSize (nameWidth + controlWidth + unitWidth + 24,
             headerHeight + rowHeight * static_cast<int> (m_controls.size()) + footerHeight);
}

RfAnalysisSettingsWindow::~RfAnalysisSettingsWindow() = default;

void RfAnalysisSettingsWindow::addControl (const char* parameterName)
{
    auto* parameter = m_node->getParameter (parameterName);

    if (parameter == nullptr)
        return;

    auto control = std::make_unique<ParameterControl> (parameter, nameWidth, controlWidth, unitWidth);

    // Deliberately left active during acquisition. See the class comment: these
    // are read-time parameters, and tuning them against a live map is what they
    // are for.
    control->setActive (true);
    addAndMakeVisible (control.get());

    m_controls.push_back (std::move (control));
}

void RfAnalysisSettingsWindow::addSectionBreak()
{
    m_controls.push_back (nullptr);
}

void RfAnalysisSettingsWindow::updatePopup()
{
    for (auto& control : m_controls)
        if (control != nullptr)
            control->refresh();

    repaint();
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
}

void RfAnalysisSettingsWindow::paint (Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (12, 0);

    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (FontOptions (14.0f));
    g.drawText ("Mapping settings", header, Justification::centredLeft);

    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (12, 0);
    g.setFont (FontOptions (11.0f));
    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
    g.drawText ("All of these re-read the accumulated trials; none discards them.",
                footer,
                Justification::centredLeft);
}

} // namespace EventTriggered
