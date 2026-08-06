/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredCoherence.
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

#include <JuceHeader.h>
#include <VisualizerWindowHeaders.h>

namespace TriggeredSpectra
{

class TriggeredCoherenceNode;

/** Display for TriggeredCoherence.
 *
 *  Redraws are event-driven: the node calls refresh() from handleAsyncUpdate()
 *  when the worker commits trials. The inherited Visualizer timer is deliberately
 *  left unstarted, so an idle plugin costs nothing.
 */
class TriggeredCoherenceCanvas : public Visualizer
{
public:
    explicit TriggeredCoherenceCanvas (TriggeredCoherenceNode* node);
    ~TriggeredCoherenceCanvas() override = default;

    void refresh() override;
    void refreshState() override;
    void updateSettings() override {}

    /** Visualizer's polling timer is unused; see the class comment. */
    void timerCallback() override {}

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    TriggeredCoherenceNode* m_node = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredCoherenceCanvas)
};

} // namespace TriggeredSpectra
