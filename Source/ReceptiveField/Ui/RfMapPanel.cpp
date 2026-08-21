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
#include "RfMapPanel.h"

#include "RfMath/AngleConvention.h"

#include <algorithm>
#include <cmath>

using namespace juce;

namespace EventTriggered
{

namespace
{
    /** The paper's colour scale: blue through cyan, green and yellow to red. */
    Colour jetColour (float t)
    {
        t = jlimit (0.0f, 1.0f, t);

        const auto channel = [t] (float centre) {
            return jlimit (0.0f, 1.0f, 1.5f - std::abs (4.0f * t - centre));
        };

        return Colour::fromFloatRGBA (channel (3.0f), channel (2.0f), channel (1.0f), 1.0f);
    }

    constexpr int labelHeight = 18;
} // namespace

// --- RfMapPanel ------------------------------------------------------------

RfMapPanel::RfMapPanel()
{
    setInterceptsMouseClicks (false, false);
}

void RfMapPanel::setChannelName (const String& name)
{
    m_channelName = name;
}

void RfMapPanel::setMapping (const Rf::ChannelMapping& mapping)
{
    m_mapping = mapping;
    rebuildImage();
    repaint();
}

void RfMapPanel::setShowPolargram (bool show)
{
    m_showPolargram = show;
    repaint();
}

void RfMapPanel::setSharedColourRange (bool shared, float low, float high)
{
    m_sharedColourRange = shared;
    m_sharedLow = low;
    m_sharedHigh = high;
    rebuildImage();
    repaint();
}

void RfMapPanel::rebuildImage()
{
    if (! m_mapping.valid || m_mapping.map.isEmpty())
    {
        m_image = Image();
        return;
    }

    const int pixels = m_mapping.map.pixels();

    float low = m_sharedLow;
    float high = m_sharedHigh;

    if (! m_sharedColourRange)
    {
        const auto& values = m_mapping.map.values();
        const auto [minIt, maxIt] = std::minmax_element (values.begin(), values.end());
        low = *minIt;
        high = *maxIt;
    }

    const float range = std::max (1e-9f, high - low);

    // Rasterised once per result rather than in paint(): a 201x201 map redrawn
    // pixel by pixel on every repaint is the one thing here that could actually
    // cost frames.
    m_image = Image (Image::RGB, pixels, pixels, false);
    Image::BitmapData data (m_image, Image::BitmapData::writeOnly);

    for (int row = 0; row < pixels; ++row)
        for (int col = 0; col < pixels; ++col)
            data.setPixelColour (col, row, jetColour ((m_mapping.map.at (row, col) - low) / range));
}

void RfMapPanel::resized() {}

void RfMapPanel::paint (Graphics& g)
{
    auto bounds = getLocalBounds().reduced (2);

    g.setColour (Colours::white);
    g.setFont (FontOptions (13.0f));

    auto labelArea = bounds.removeFromTop (labelHeight);
    g.drawText (m_channelName, labelArea, Justification::centredLeft, true);

    if (m_mapping.valid && m_mapping.estimate.valid && m_mapping.estimate.equivalentDiameterDeg > 0.0)
    {
        g.setColour (Colours::lightgrey);
        g.setFont (FontOptions (11.0f));
        g.drawText (String (m_mapping.estimate.equivalentDiameterDeg, 1) + " deg  z="
                        + String (m_mapping.estimate.peak, 1) + "  n=" + String (m_mapping.minimumTrialCount),
                    labelArea,
                    Justification::centredRight,
                    true);
    }

    // Square, so degrees per pixel is the same in x and y. A stretched map would
    // make a circular receptive field look elliptical, which is a property people
    // read off these pictures.
    const int side = std::min (bounds.getWidth(), bounds.getHeight());
    const Rectangle<int> mapArea =
        Rectangle<int> (side, side).withCentre (bounds.getCentre());

    if (! m_image.isValid())
    {
        g.setColour (Colours::darkgrey);
        g.drawRect (mapArea, 1);
        g.setFont (FontOptions (12.0f));
        g.drawText ("no data", mapArea, Justification::centred, true);
        return;
    }

    g.drawImage (m_image, mapArea.toFloat());

    const Rf::MapGeometry& geometry = m_mapping.map.geometry();
    const double scale = static_cast<double> (mapArea.getWidth()) / geometry.pixels;

    const auto toScreen = [&] (double xDeg, double yDeg) {
        const double col = (xDeg - geometry.centreXDeg) / geometry.degreesPerPixel + geometry.centreIndex();
        const double row = geometry.centreIndex() - (yDeg - geometry.centreYDeg) / geometry.degreesPerPixel;
        return Point<float> (static_cast<float> (mapArea.getX() + col * scale),
                             static_cast<float> (mapArea.getY() + row * scale));
    };

    if (m_mapping.estimate.valid && m_mapping.estimate.equivalentDiameterDeg > 0.0)
    {
        const Point<float> centre =
            toScreen (m_mapping.estimate.centreXDeg, m_mapping.estimate.centreYDeg);

        const auto radius = static_cast<float> (
            0.5 * m_mapping.estimate.equivalentDiameterDeg / geometry.degreesPerPixel * scale);

        g.setColour (Colours::black);
        g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);

        g.setColour (Colours::white);
        g.drawLine (centre.x - 6.0f, centre.y, centre.x + 6.0f, centre.y, 1.5f);
        g.drawLine (centre.x, centre.y - 6.0f, centre.x, centre.y + 6.0f, 1.5f);
    }

    if (m_showPolargram)
        paintPolargram (g, mapArea);

    g.setColour (Colours::darkgrey);
    g.drawRect (mapArea, 1);
}

void RfMapPanel::paintPolargram (Graphics& g, Rectangle<int> area) const
{
    if (m_mapping.responses.empty()
        || m_mapping.responses.size() != m_mapping.canonicalAnglesDeg.size())
        return;

    const float strongest =
        *std::max_element (m_mapping.responses.begin(), m_mapping.responses.end());

    if (! (strongest > 0.0f))
        return;

    const int side = std::max (36, area.getWidth() / 4);
    const Rectangle<int> inset = area.removeFromBottom (side).removeFromRight (side).reduced (3);
    const Point<float> centre = inset.getCentre().toFloat();
    const float radius = inset.getWidth() * 0.5f;

    g.setColour (Colours::black.withAlpha (0.45f));
    g.fillEllipse (inset.toFloat());

    Path path;
    bool started = false;

    for (std::size_t i = 0; i < m_mapping.responses.size(); ++i)
    {
        const double rad = Rf::degToRad (m_mapping.canonicalAnglesDeg[i]);
        const float r = radius * jlimit (0.0f, 1.0f, m_mapping.responses[i] / strongest);

        // Screen y grows downwards while visual-field y grows upwards, so the
        // polargram is flipped here to match the map above it. Drawn the other
        // way it would show the preferred direction mirrored, next to a map that
        // is not.
        const Point<float> point (centre.x + r * static_cast<float> (std::cos (rad)),
                                  centre.y - r * static_cast<float> (std::sin (rad)));

        if (! started)
        {
            path.startNewSubPath (point);
            started = true;
        }
        else
        {
            path.lineTo (point);
        }
    }

    path.closeSubPath();

    g.setColour (Colours::yellow.withAlpha (0.9f));
    g.strokePath (path, PathStrokeType (1.2f));
}

// --- RfMapGrid -------------------------------------------------------------

RfMapGrid::RfMapGrid() {}

void RfMapGrid::setResults (const RfResults& results, const StringArray& channelNames)
{
    // Rebuild only when the shape changed. Otherwise the panels are reused and
    // just given new data, so a refresh does not churn components while the user
    // is looking at them.
    if (static_cast<int> (results.channels.size()) != m_panels.size())
    {
        m_panels.clear();

        for (std::size_t i = 0; i < results.channels.size(); ++i)
            addAndMakeVisible (m_panels.add (new RfMapPanel()));

        resized();
    }

    m_mappings = results.channels;

    for (int i = 0; i < m_panels.size(); ++i)
    {
        m_panels[i]->setChannelName (i < channelNames.size() ? channelNames[i]
                                                             : "CH " + String (i + 1));
        m_panels[i]->setShowPolargram (m_showPolargram);
        m_panels[i]->setMapping (m_mappings[static_cast<std::size_t> (i)]);
    }

    applyColourRange();
}

void RfMapGrid::applyColourRange()
{
    if (! m_sharedColourRange)
    {
        for (auto* panel : m_panels)
            panel->setSharedColourRange (false, 0.0f, 1.0f);

        return;
    }

    float low = std::numeric_limits<float>::max();
    float high = std::numeric_limits<float>::lowest();

    for (const Rf::ChannelMapping& mapping : m_mappings)
    {
        if (! mapping.valid || mapping.map.isEmpty())
            continue;

        const auto [minIt, maxIt] =
            std::minmax_element (mapping.map.values().begin(), mapping.map.values().end());
        low = std::min (low, *minIt);
        high = std::max (high, *maxIt);
    }

    if (low > high)
        return;

    for (auto* panel : m_panels)
        panel->setSharedColourRange (true, low, high);
}

void RfMapGrid::setNumColumns (int columns)
{
    m_columns = jmax (1, columns);
    resized();
}

void RfMapGrid::setPanelHeight (int pixels)
{
    m_panelHeight = jmax (80, pixels);
    resized();
}

void RfMapGrid::setShowPolargram (bool show)
{
    m_showPolargram = show;

    for (auto* panel : m_panels)
        panel->setShowPolargram (show);
}

void RfMapGrid::setSharedColourRange (bool shared)
{
    m_sharedColourRange = shared;
    applyColourRange();
}

int RfMapGrid::getDesiredHeight() const
{
    const int rows = (m_panels.size() + m_columns - 1) / jmax (1, m_columns);
    return jmax (m_panelHeight, rows * m_panelHeight);
}

void RfMapGrid::resized()
{
    if (m_panels.isEmpty())
        return;

    // Cells are square, not "the viewport divided by the column count". A map is
    // square and is centred in whatever cell it gets, so a stretched cell shows
    // up purely as blank space between the columns -- three 220 px maps spread
    // across a 1800 px window sat with 300 px of black between each of them.
    // Sizing the cell from the panel height instead keeps the maps adjacent and
    // makes the Size control mean what it says.
    const int cell = jmax (40, m_panelHeight);
    const int used = cell * m_columns;

    // Left-aligned once the row is wider than the viewport (the horizontal
    // scrollbar is off, so a negative offset would hide the first column).
    const int xOffset = jmax (0, (getWidth() - used) / 2);

    for (int i = 0; i < m_panels.size(); ++i)
    {
        const int row = i / m_columns;
        const int col = i % m_columns;
        m_panels[i]->setBounds (xOffset + col * cell, row * m_panelHeight, cell, m_panelHeight);
    }
}

void RfMapGrid::paint (Graphics& g)
{
    if (m_panels.isEmpty())
    {
        g.setColour (Colours::grey);
        g.setFont (FontOptions (15.0f));
        g.drawText ("No maps yet. Select channels, configure directions under STIMULUS, "
                    "and record some trials.",
                    getLocalBounds().reduced (20),
                    Justification::centredTop,
                    true);
    }
}

} // namespace EventTriggered
