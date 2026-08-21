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
#pragma once

#include "../RfComputeJob.h"
#include "RfMath/RfPipeline.h"

#include <VisualizerWindowHeaders.h>

namespace EventTriggered
{

/** One channel's back-projection map, with its contour, peak marker and
 *  polargram.
 *
 *  Draws a finished Rf::ChannelMapping and holds no reference to anything that
 *  computes one, so it can be handed a result from the compute thread by value
 *  and never has to reason about whether its inputs are still alive.
 */
class RfMapPanel : public juce::Component
{
public:
    RfMapPanel();

    void setChannelName (const juce::String& name);

    /** Replaces what is drawn. Cheap enough to call on every refresh: the map is
        rasterised into an image here, not per paint(). */
    void setMapping (const Rf::ChannelMapping& mapping);

    /** Draws the polargram inset over the map. */
    void setShowPolargram (bool show);

    /** Fixes the colour scale across panels, so channels are comparable.
        Passing an empty range restores per-panel auto-scaling. */
    void setSharedColourRange (bool shared, float low, float high);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void rebuildImage();
    void paintPolargram (juce::Graphics& g, juce::Rectangle<int> area) const;

    juce::String m_channelName;
    Rf::ChannelMapping m_mapping;
    juce::Image m_image;

    bool m_showPolargram = true;
    bool m_sharedColourRange = false;
    float m_sharedLow = 0.0f;
    float m_sharedHigh = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RfMapPanel)
};

/** The grid of per-channel maps. */
class RfMapGrid : public juce::Component
{
public:
    RfMapGrid();

    /** Rebuilds the panels from a full set of results. */
    void setResults (const RfResults& results, const juce::StringArray& channelNames);

    void setNumColumns (int columns);
    void setPanelHeight (int pixels);
    void setShowPolargram (bool show);

    /** One colour scale for every panel, so a strong channel and a weak one are
     *  visibly different rather than both rendered full-scale. Off by default,
     *  because when hunting for any response at all, per-panel scaling is what
     *  makes a weak one visible. */
    void setSharedColourRange (bool shared);

    int getDesiredHeight() const;
    void resized() override;
    void paint (juce::Graphics& g) override;

private:
    void applyColourRange();

    juce::OwnedArray<RfMapPanel> m_panels;
    std::vector<Rf::ChannelMapping> m_mappings;

    int m_columns = 4;
    int m_panelHeight = 220;
    bool m_sharedColourRange = false;
    bool m_showPolargram = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RfMapGrid)
};

} // namespace EventTriggered
