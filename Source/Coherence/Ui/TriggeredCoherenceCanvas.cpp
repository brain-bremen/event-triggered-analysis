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
#include "TriggeredCoherenceCanvas.h"

#include "../TriggeredCoherenceNode.h"

namespace TriggeredSpectra
{

TriggeredCoherenceCanvas::TriggeredCoherenceCanvas (TriggeredCoherenceNode* node) : m_node (node) {}

void TriggeredCoherenceCanvas::refresh() { repaint(); }

void TriggeredCoherenceCanvas::refreshState() { resized(); }

void TriggeredCoherenceCanvas::resized() {}

void TriggeredCoherenceCanvas::paint (juce::Graphics& g)
{
    g.fillAll (findColour (ThemeColours::componentBackground));

    if (m_node == nullptr)
        return;

    // Phase 1 status view. The grid of spectrogram / line panels replaces this in
    // Phase 3; showing the trial counts here is what makes the capture path
    // verifiable end to end in the GUI.
    g.setColour (findColour (ThemeColours::defaultText));
    g.setFont (juce::FontOptions (16.0f));

    int y = 20;
    g.drawText ("Triggered Coherence", 20, y, getWidth() - 40, 24, juce::Justification::left);
    y += 34;

    g.setFont (juce::FontOptions (13.0f));

    const auto& geometry = m_node->getTrialGeometry();

    g.drawText (juce::String ("Sample rate: ") + juce::String (geometry.sampleRate, 1) + " Hz",
                20,
                y,
                getWidth() - 40,
                18,
                juce::Justification::left);
    y += 20;

    g.drawText (juce::String ("Window: ") + juce::String (geometry.preSamples) + " pre + "
                    + juce::String (geometry.postSamples) + " post samples (+"
                    + juce::String (geometry.padSamples) + " pad each side)",
                20,
                y,
                getWidth() - 40,
                18,
                juce::Justification::left);
    y += 20;

    g.drawText (juce::String ("Channels selected: ")
                    + juce::String (m_node->getSelectedChannels().size()),
                20,
                y,
                getWidth() - 40,
                18,
                juce::Justification::left);
    y += 28;

    const int numFrequencies = m_node->getNumFrequencies();
    const int numBins = m_node->getNumBins();
    const auto frequencies = m_node->getFrequencies();
    const auto& pairs = m_node->getPairs();

    g.drawText (juce::String ("Estimator: ") + juce::String (numFrequencies) + " frequencies x "
                    + juce::String (numBins) + " bins    pairs: " + juce::String (pairs.size()),
                20,
                y,
                getWidth() - 40,
                18,
                juce::Justification::left);
    y += 20;

    if (pairs.empty())
    {
        g.setColour (juce::Colours::orange);
        g.drawText ("No channel pairs configured - coherence needs at least one.",
                    20,
                    y,
                    getWidth() - 40,
                    18,
                    juce::Justification::left);
        y += 20;
        g.setColour (findColour (ThemeColours::defaultText));
    }

    y += 8;

    const auto lock = m_node->lockData();

    std::vector<double> values (static_cast<std::size_t> (std::max (1, numBins)));

    for (auto* source : m_node->getTriggerSources().getAll())
    {
        g.setColour (source->colour);
        g.fillRect (20, y + 4, 10, 10);

        const int numTrials = m_node->getNumTrials (source);
        const int dof = m_node->getDegreesOfFreedom (source);
        const double threshold = m_node->getSignificanceThreshold (source);

        g.setColour (findColour (ThemeColours::defaultText));
        g.drawText (source->name + "  trials: " + juce::String (numTrials) + "   dof: "
                        + juce::String (dof) + "   p<0.05 above: "
                        + (dof >= 2 ? juce::String (threshold, 3) : juce::String ("n/a")),
                    38,
                    y,
                    getWidth() - 58,
                    18,
                    juce::Justification::left);
        y += 20;

        // Peak coherence per pair, with the significance call, so the estimate can
        // be sanity-checked before the real display exists.
        for (int pairIndex = 0; pairIndex < static_cast<int> (pairs.size()); ++pairIndex)
        {
            double best = 0.0;
            int argmax = 0;
            bool any = false;

            for (int f = 0; f < numFrequencies; ++f)
            {
                if (! m_node->getCoherenceForDisplay (source, pairIndex, f, values))
                    continue;

                any = true;

                for (int bin = 0; bin < numBins; ++bin)
                {
                    if (values[static_cast<std::size_t> (bin)] > best)
                    {
                        best = values[static_cast<std::size_t> (bin)];
                        argmax = f;
                    }
                }
            }

            const auto& pair = pairs[static_cast<std::size_t> (pairIndex)];

            juce::String line = "    " + pair.name;

            if (! pair.isResolved())
                line += "  (channels not selected)";
            else if (any && ! frequencies.empty())
                line += "  peak " + juce::String (best, 3) + " at "
                        + juce::String (frequencies[static_cast<std::size_t> (argmax)], 1) + " Hz"
                        + (best > threshold ? "  *" : "");

            g.setColour (pair.colour);
            g.drawText (line, 48, y, getWidth() - 68, 16, juce::Justification::left);
            y += 18;
        }

        y += 6;
    }

    if (const int dropped = m_node->getNumDroppedRequests(); dropped > 0)
    {
        y += 10;
        g.setColour (juce::Colours::orange);
        g.drawText (juce::String (dropped) + " trigger(s) dropped: worker queue full",
                    20,
                    y,
                    getWidth() - 40,
                    18,
                    juce::Justification::left);
    }
}

} // namespace TriggeredSpectra
