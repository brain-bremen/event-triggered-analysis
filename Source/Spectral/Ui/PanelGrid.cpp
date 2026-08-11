/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2026 Joscha Schmiedt, Universität Bremen

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
#include "PanelGrid.h"

#include <algorithm>

namespace EventTriggered
{

void PanelGrid::setNumPanels (int numPanels)
{
    numPanels = std::max (0, numPanels);

    if (numPanels == m_panels.size())
        return;

    // Grow or shrink in place rather than rebuilding: a channel-selection change
    // should not throw away per-panel scale settings the user has adjusted.
    while (m_panels.size() > numPanels)
    {
        removeChildComponent (m_panels.getLast());
        m_panels.removeLast();
    }

    while (m_panels.size() < numPanels)
    {
        auto* panel = m_panels.add (new SpectrumPanel());
        addAndMakeVisible (panel);
    }

    resized();
}

void PanelGrid::setColumns (int columns)
{
    columns = std::max (1, columns);

    if (columns == m_columns)
        return;

    m_columns = columns;
    resized();
}

void PanelGrid::setPanelHeight (int height)
{
    height = std::max (80, height);

    if (height == m_panelHeight)
        return;

    m_panelHeight = height;
    resized();
}

int PanelGrid::getRequiredHeight() const
{
    if (m_panels.isEmpty())
        return 0;

    const int rows = (m_panels.size() + m_columns - 1) / m_columns;
    return rows * m_panelHeight;
}

void PanelGrid::repaintPanels()
{
    for (auto* panel : m_panels)
        panel->repaint();
}

void PanelGrid::resized()
{
    if (m_panels.isEmpty() || getWidth() <= 0)
        return;

    const int columnWidth = std::max (1, getWidth() / m_columns);

    for (int i = 0; i < m_panels.size(); ++i)
    {
        const int column = i % m_columns;
        const int row = i / m_columns;

        m_panels[i]->setBounds (
            column * columnWidth, row * m_panelHeight, columnWidth, m_panelHeight);
    }
}

} // namespace EventTriggered
