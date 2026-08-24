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

#include "../RfMath/AngleConvention.h"

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

        const auto channel = [t] (float centre)
        { return jlimit (0.0f, 1.0f, 1.5f - std::abs (4.0f * t - centre)); };

        return Colour::fromFloatRGBA (channel (3.0f), channel (2.0f), channel (1.0f), 1.0f);
    }

    constexpr int labelHeight = 18;

    /** The colour scale's own column, to the right of the map: the gradient
     *  strip, its numbers, and the caption naming the unit underneath.
     *
     *  Beside the map rather than inset over it. The polargram already covers one
     *  corner, and a scale that hides map pixels is a scale that has to be
     *  switched off to read the thing it explains. */
    constexpr int scaleStripWidth = 10;
    constexpr int scaleNumberWidth = 34;
    constexpr int scaleGap = 4;
    constexpr int scaleCaptionHeight = 12;
    constexpr int scaleColumnWidth = scaleGap + scaleStripWidth + 2 + scaleNumberWidth;

    /** Enough digits to tell two ends of the scale apart, and no more: the
        numbers sit in 34 px. */
    String formatScaleValue (float value)
    {
        const float magnitude = std::abs (value);

        if (magnitude >= 100.0f)
            return String (roundToInt (value));
        if (magnitude >= 10.0f)
            return String (value, 1);
        if (magnitude >= 1.0f)
            return String (value, 2);

        return String (value, 3);
    }

    /** The degree sign, as an explicit code point rather than a literal: the
        source file's encoding is not something a build should have to be right
        about. Same reasoning as SweepAngles::generateDirections. */
    String degreeSign() { return String::charToString (static_cast<juce_wchar> (0x00B0)); }

    /** What one map value is, given how it was made.
     *
     *  The profiles are z-scored per direction, so a map pixel is in units of the
     *  spontaneous SD of that channel -- but only until the combine mode changes.
     *  A mean or a signed geometric mean of z-scores is still a z-score; a plain
     *  product of n of them is a z to the nth, and calling that "z" is how a
     *  number that grew by a factor of 100 gets read as a stronger response. */
    String mapValueUnit (const Rf::MappingSettings& settings)
    {
        const String base = settings.profile.useAbsoluteValue ? "|z|" : "z";

        return settings.backProjection.combine == Rf::CombineMode::Product ? base + "^n" : base;
    }
} // namespace

// --- RfMapPanel ------------------------------------------------------------

RfMapPanel::RfMapPanel() { setInterceptsMouseClicks (false, false); }

void RfMapPanel::setChannelName (const String& name) { m_channelName = name; }

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

void RfMapPanel::setValueUnit (const String& unit)
{
    if (m_valueUnit == unit)
        return;

    m_valueUnit = unit;
    repaint();
}

std::pair<float, float> RfMapPanel::colourRange() const
{
    if (m_sharedColourRange)
        return { m_sharedLow, m_sharedHigh };

    if (m_mapping.map.isEmpty())
        return { 0.0f, 1.0f };

    const auto& values = m_mapping.map.values();
    const auto [minIt, maxIt] = std::minmax_element (values.begin(), values.end());
    return { *minIt, *maxIt };
}

void RfMapPanel::rebuildImage()
{
    if (! m_mapping.valid || m_mapping.map.isEmpty())
    {
        m_image = Image();
        return;
    }

    const int pixels = m_mapping.map.pixels();

    const auto [low, high] = colourRange();
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

    // What the numbers on this line are is the whole reason they are spelled out
    // rather than abbreviated: "2.4 deg z=5.3 n=12" needed a key to read, and the
    // one number it did label -- the peak -- is now the top of the colour scale,
    // where it says what it is by standing next to the colour it belongs to.
    if (m_mapping.valid && m_mapping.estimate.valid
        && m_mapping.estimate.equivalentDiameterDeg > 0.0)
    {
        g.setColour (Colours::lightgrey);
        g.setFont (FontOptions (11.0f));
        g.drawText ("RF " + String (m_mapping.estimate.equivalentDiameterDeg, 1) + degreeSign()
                        + "   n = " + String (m_mapping.minimumTrialCount),
                    labelArea,
                    Justification::centredRight,
                    true);
    }

    // The colour scale takes its column before the map is sized, so the map stays
    // square rather than being squeezed into what is left over.
    const bool showScale = m_image.isValid() && bounds.getWidth() > scaleColumnWidth * 3;
    Rectangle<int> scaleArea =
        showScale ? bounds.removeFromRight (scaleColumnWidth) : Rectangle<int>();

    // Square, so degrees per pixel is the same in x and y. A stretched map would
    // make a circular receptive field look elliptical, which is a property people
    // read off these pictures.
    const int side = std::min (bounds.getWidth(), bounds.getHeight());
    const Rectangle<int> mapArea = Rectangle<int> (side, side).withCentre (bounds.getCentre());

    if (! m_image.isValid())
    {
        g.setColour (Colours::darkgrey);
        g.drawRect (mapArea, 1);
        g.setFont (FontOptions (12.0f));
        g.drawText ("no data", mapArea, Justification::centred, true);
        return;
    }

    g.drawImage (m_image, mapArea.toFloat());

    if (showScale)
        paintColourScale (g, scaleArea.withY (mapArea.getY()).withHeight (mapArea.getHeight()));

    const Rf::MapGeometry& geometry = m_mapping.map.geometry();
    const double scale = static_cast<double> (mapArea.getWidth()) / geometry.pixels;

    const auto toScreen = [&] (double xDeg, double yDeg)
    {
        const double col =
            (xDeg - geometry.centreXDeg) / geometry.degreesPerPixel + geometry.centreIndex();
        const double row =
            geometry.centreIndex() - (yDeg - geometry.centreYDeg) / geometry.degreesPerPixel;
        return Point<float> (static_cast<float> (mapArea.getX() + col * scale),
                             static_cast<float> (mapArea.getY() + row * scale));
    };

    if (m_mapping.estimate.valid && m_mapping.estimate.equivalentDiameterDeg > 0.0)
    {
        const Point<float> centre =
            toScreen (m_mapping.estimate.centreXDeg, m_mapping.estimate.centreYDeg);

        const auto radius = static_cast<float> (0.5 * m_mapping.estimate.equivalentDiameterDeg
                                                / geometry.degreesPerPixel * scale);

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

void RfMapPanel::paintColourScale (Graphics& g, Rectangle<int> area) const
{
    const auto [low, high] = colourRange();

    area.removeFromLeft (scaleGap);
    const Rectangle<int> caption = area.removeFromBottom (scaleCaptionHeight);

    Rectangle<int> strip = area.removeFromLeft (scaleStripWidth);
    area.removeFromLeft (2);

    if (strip.getHeight() < 8)
        return;

    // Drawn line by line rather than as a ColourGradient: jet is not a linear
    // interpolation between two colours, and a two-stop gradient would draw a
    // scale that does not match the image it is a key to.
    for (int y = 0; y < strip.getHeight(); ++y)
    {
        const float t = 1.0f - static_cast<float> (y) / static_cast<float> (strip.getHeight() - 1);
        g.setColour (jetColour (t));
        g.fillRect (strip.getX(), strip.getY() + y, strip.getWidth(), 1);
    }

    g.setColour (Colours::darkgrey);
    g.drawRect (strip, 1);

    // Where this panel's own peak falls on a scale it does not set. Only under
    // SAME SCALE: with per-panel scaling the peak is the top of the bar by
    // construction, and a tick there says nothing.
    if (m_sharedColourRange && m_mapping.estimate.valid && high > low)
    {
        const float t = jlimit (0.0f, 1.0f, (m_mapping.estimate.peak - low) / (high - low));
        const int y = strip.getBottom() - roundToInt (t * (strip.getHeight() - 1));

        g.setColour (Colours::white);
        g.drawLine (static_cast<float> (strip.getX() - 3),
                    static_cast<float> (y),
                    static_cast<float> (strip.getRight() + 3),
                    static_cast<float> (y),
                    1.0f);
    }

    g.setFont (FontOptions (10.0f));
    g.setColour (Colours::lightgrey);

    const auto label = [&] (float value, int y, Justification justification)
    {
        g.drawText (formatScaleValue (value),
                    Rectangle<int> (area.getX(), y, area.getWidth(), 11),
                    justification,
                    false);
    };

    label (high, strip.getY(), Justification::centredLeft);
    label (0.5f * (low + high), strip.getCentreY() - 5, Justification::centredLeft);
    label (low, strip.getBottom() - 11, Justification::centredLeft);

    g.setFont (FontOptions (10.0f));
    g.setColour (Colours::grey);
    g.drawText (m_valueUnit, caption, Justification::centredLeft, false);
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
    m_valueUnit = mapValueUnit (results.settings);

    for (int i = 0; i < m_panels.size(); ++i)
    {
        m_panels[i]->setChannelName (i < channelNames.size() ? channelNames[i]
                                                             : "CH " + String (i + 1));
        m_panels[i]->setShowPolargram (m_showPolargram);
        m_panels[i]->setValueUnit (m_valueUnit);
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

    // Nothing valid to share a scale over -- an empty grid, or every map still
    // waiting for its first trial. Falling back to per-panel scaling rather than
    // returning: leaving the panels on whatever range was last shared would
    // label the next map with numbers from the previous one.
    if (low > high)
    {
        for (auto* panel : m_panels)
            panel->setSharedColourRange (false, 0.0f, 1.0f);

        return;
    }

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
        g.drawText ("No maps yet. Select channels, configure directions under SWEEPS, "
                    "and record some trials.",
                    getLocalBounds().reduced (20),
                    Justification::centredTop,
                    true);
    }
}

} // namespace EventTriggered
