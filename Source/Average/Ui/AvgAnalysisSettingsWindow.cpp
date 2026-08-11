/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Triggered Average
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
#include "AvgAnalysisSettingsWindow.h"

#include "../TriggeredAvgNode.h"

namespace EventTriggered
{

AvgAnalysisSettingsWindow::AvgAnalysisSettingsWindow (TriggeredAvgNode* node,
                                                      bool acquisitionIsActive,
                                                      juce::Component* anchor)
    : PopupComponent (anchor), m_node (node), m_acquisitionIsActive (acquisitionIsActive)
{
    jassert (anchor != nullptr); // PopupComponent dereferences it in its constructor

    if (m_node != nullptr)
    {
        if (auto* parameter = m_node->getParameter (ParameterNames::max_trials))
        {
            m_maxTrialsControl =
                std::make_unique<ParameterControl> (parameter, nameWidth, controlWidth, unitWidth);
            m_maxTrialsControl->setActive (! m_acquisitionIsActive);
            addAndMakeVisible (m_maxTrialsControl.get());
        }
    }

    setSize (nameWidth + controlWidth + unitWidth + 24, headerHeight + rowHeight + footerHeight);
}

AvgAnalysisSettingsWindow::~AvgAnalysisSettingsWindow() = default;

void AvgAnalysisSettingsWindow::updatePopup()
{
    if (m_maxTrialsControl != nullptr)
        m_maxTrialsControl->refresh();

    repaint();
}

void AvgAnalysisSettingsWindow::resized()
{
    if (m_maxTrialsControl != nullptr)
        m_maxTrialsControl->setBounds (12, headerHeight, getWidth() - 24, rowHeight);
}

void AvgAnalysisSettingsWindow::paint (juce::Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    auto header = getLocalBounds().removeFromTop (headerHeight).reduced (12, 0);

    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (juce::FontOptions (14.0f));
    g.drawText ("Analysis settings", header, juce::Justification::centredLeft);

    auto footer = getLocalBounds().removeFromBottom (footerHeight).reduced (12, 0);

    g.setFont (juce::FontOptions (11.0f));

    if (m_acquisitionIsActive)
    {
        g.setColour (juce::Colours::orange);
        g.drawText ("Stop acquisition to change this - it reallocates the trial buffers.",
                    footer,
                    juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
        g.drawText ("Individual trials retained per condition.", footer, juce::Justification::centredLeft);
    }
}

} // namespace EventTriggered
