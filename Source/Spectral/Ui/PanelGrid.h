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
#pragma once

#include "SpectrumPanel.h"

#include <JuceHeader.h>

namespace EventTriggered
{

/** Lays SpectrumPanels out in a scrollable grid.
 *
 *  Owns the panels. The canvas asks for a panel count, configures each one, then
 *  calls repaintPanels() — it never has to think about layout or lifetime.
 */
class PanelGrid : public juce::Component
{
public:
    PanelGrid() = default;

    /** Resizes the grid, creating or destroying panels as needed. Existing
        panels keep their settings, so a channel-count change does not reset the
        user's scale choices. */
    void setNumPanels (int numPanels);

    int getNumPanels() const { return m_panels.size(); }
    SpectrumPanel* getPanel (int index) const { return m_panels[index]; }

    void setColumns (int columns);
    int getColumns() const noexcept { return m_columns; }

    void setPanelHeight (int height);
    int getPanelHeight() const noexcept { return m_panelHeight; }

    /** Height needed to show every panel, for the enclosing Viewport. */
    int getRequiredHeight() const;

    void repaintPanels();

    void resized() override;

private:
    juce::OwnedArray<SpectrumPanel> m_panels;

    int m_columns = 2;
    int m_panelHeight = 180;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PanelGrid)
};

} // namespace EventTriggered
