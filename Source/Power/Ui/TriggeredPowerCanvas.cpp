/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredPower.
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
#include "TriggeredPowerCanvas.h"

#include "../TriggeredPowerNode.h"

namespace TriggeredSpectra
{

TriggeredPowerCanvas::TriggeredPowerCanvas (TriggeredPowerNode* node) : m_node (node) {}

void TriggeredPowerCanvas::refresh() { repaint(); }

void TriggeredPowerCanvas::refreshState() { resized(); }

void TriggeredPowerCanvas::resized() {}

void TriggeredPowerCanvas::paint (juce::Graphics& g)
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
    g.drawText ("Triggered Power", 20, y, getWidth() - 40, 24, juce::Justification::left);
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

    g.drawText (juce::String ("Estimator: ") + juce::String (numFrequencies) + " frequencies x "
                    + juce::String (numBins) + " bins"
                    + (frequencies.empty()
                           ? juce::String()
                           : juce::String (" (") + juce::String (frequencies.front(), 1) + " - "
                                 + juce::String (frequencies.back(), 1) + " Hz)"),
                20,
                y,
                getWidth() - 40,
                18,
                juce::Justification::left);
    y += 28;

    const auto lock = m_node->lockData();

    std::vector<double> values (static_cast<std::size_t> (std::max (1, numBins)));

    for (auto* source : m_node->getTriggerSources().getAll())
    {
        g.setColour (source->colour);
        g.fillRect (20, y + 4, 10, 10);

        const int numTrials = m_node->getNumTrials (source);

        juce::String line = source->name + "  (TTL line " + juce::String (source->line)
                            + ")   trials: " + juce::String (numTrials);

        // Report where the power actually is, so the plugin can be sanity-checked
        // against a known input before the real display exists.
        if (numTrials > 0 && numFrequencies > 0 && ! frequencies.empty())
        {
            double best = -std::numeric_limits<double>::max();
            int argmax = 0;

            for (int f = 0; f < numFrequencies; ++f)
            {
                if (! m_node->getPowerForDisplay (source, 0, f, values))
                    continue;

                for (int bin = 0; bin < numBins; ++bin)
                {
                    if (values[static_cast<std::size_t> (bin)] > best)
                    {
                        best = values[static_cast<std::size_t> (bin)];
                        argmax = f;
                    }
                }
            }

            if (best > -std::numeric_limits<double>::max())
                line += "   peak ch0: " + juce::String (frequencies[static_cast<std::size_t> (argmax)], 1)
                        + " Hz";
        }

        g.setColour (findColour (ThemeColours::defaultText));
        g.drawText (line, 38, y, getWidth() - 58, 18, juce::Justification::left);
        y += 22;
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
