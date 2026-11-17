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
#include "RfMapAreaPanel.h"

#include "../BarMapperNode.h"

using namespace juce;

namespace EventTriggered
{

namespace
{
    std::unique_ptr<ParameterControl> makeControl (BarMapperNode* node,
                                                   const char* name,
                                                   int nameWidth,
                                                   int controlWidth,
                                                   int unitWidth)
    {
        auto* parameter = node->getParameter (name);

        if (parameter == nullptr)
            return nullptr;

        auto control = std::make_unique<ParameterControl> (parameter, nameWidth, controlWidth, unitWidth);

        // Live during acquisition: moving the window is what you do while
        // watching a map fill in.
        control->setActive (true);
        return control;
    }
} // namespace

RfMapAreaPanel::RfMapAreaPanel (BarMapperNode* node) : m_node (node)
{
    if (m_node == nullptr)
        return;

    m_mapSize = makeControl (m_node, RfParameterNames::map_pixels, nameWidth, controlWidth, unitWidth);
    m_resolution = makeControl (m_node, RfParameterNames::mm_per_pixel, nameWidth, controlWidth, unitWidth);
    m_centreX = makeControl (m_node, RfParameterNames::map_centre_x_mm, nameWidth, controlWidth, unitWidth);
    m_centreY = makeControl (m_node, RfParameterNames::map_centre_y_mm, nameWidth, controlWidth, unitWidth);

    for (auto* control : rows())
        addAndMakeVisible (control);

    applyDisplayUnits();

    setSize (nameWidth + controlWidth + unitWidth + 24,
             headerHeight + rowHeight * static_cast<int> (rows().size()) + footerHeight);

    // Often enough that a wheel turn shows up in the fields as it happens,
    // rarely enough to cost nothing.
    startTimerHz (10);
}

RfMapAreaPanel::~RfMapAreaPanel() { stopTimer(); }

std::vector<ParameterControl*> RfMapAreaPanel::rows() const
{
    std::vector<ParameterControl*> result;

    for (const auto* control : { &m_mapSize, &m_resolution, &m_centreX, &m_centreY })
        if (*control != nullptr)
            result.push_back (control->get());

    return result;
}

void RfMapAreaPanel::applyDisplayUnits()
{
    if (m_node == nullptr)
        return;

    m_appliedScale = m_node->getDisplayUnitsPerMillimetre();
    m_appliedSuffix = Rf::unitSuffix (m_node->getDisplayUnit());

    // Map size is a count of map pixels and has no unit to convert.
    for (auto* control : { m_resolution.get(), m_centreX.get(), m_centreY.get() })
        if (control != nullptr)
            control->setDisplayTransform (m_appliedScale, m_appliedSuffix);
}

void RfMapAreaPanel::timerCallback()
{
    if (m_node == nullptr)
        return;

    if (m_node->getDisplayUnitsPerMillimetre() != m_appliedScale
        || String (Rf::unitSuffix (m_node->getDisplayUnit())) != m_appliedSuffix)
    {
        applyDisplayUnits();
        return;
    }

    // A field being typed into is left alone: refreshing it ten times a second
    // would throw away every keystroke.
    for (auto* control : rows())
        if (! control->hasKeyboardFocus (true))
            control->refresh();
}

void RfMapAreaPanel::paint (Graphics& g)
{
    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (FontOptions (14.0f));
    g.drawText ("Map area", 12, 6, getWidth() - 24, headerHeight - 8, Justification::centredLeft);

    g.setColour (findColour (ThemeColours::defaultText).withAlpha (0.6f));
    g.setFont (FontOptions (11.0f));
    g.drawFittedText ("Wheel zooms, drag pans, double-click fits the sweep.",
                      12,
                      getHeight() - footerHeight + 4,
                      getWidth() - 24,
                      footerHeight - 8,
                      Justification::centredLeft,
                      2);
}

void RfMapAreaPanel::resized()
{
    int y = headerHeight;

    for (auto* control : rows())
    {
        control->setBounds (12, y, getWidth() - 24, rowHeight - 2);
        y += rowHeight;
    }
}

} // namespace EventTriggered
