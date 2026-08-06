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
#include "ColorMap.h"

#include <algorithm>
#include <cmath>

namespace TriggeredSpectra
{

namespace
{

struct ControlPoint
{
    float position;
    float red, green, blue;
};

/** Viridis, sampled at nine control points. Perceptually uniform and
    colour-blind safe; the standard choice for a spectrogram. */
constexpr ControlPoint viridis[] = {
    { 0.000f, 0.267f, 0.005f, 0.329f }, { 0.125f, 0.283f, 0.141f, 0.458f },
    { 0.250f, 0.254f, 0.265f, 0.530f }, { 0.375f, 0.207f, 0.372f, 0.553f },
    { 0.500f, 0.164f, 0.471f, 0.558f }, { 0.625f, 0.128f, 0.567f, 0.551f },
    { 0.750f, 0.135f, 0.659f, 0.518f }, { 0.875f, 0.267f, 0.749f, 0.441f },
    { 1.000f, 0.993f, 0.906f, 0.144f }
};

constexpr ControlPoint magma[] = {
    { 0.000f, 0.001f, 0.000f, 0.014f }, { 0.125f, 0.113f, 0.065f, 0.276f },
    { 0.250f, 0.283f, 0.100f, 0.422f }, { 0.375f, 0.446f, 0.153f, 0.470f },
    { 0.500f, 0.612f, 0.201f, 0.466f }, { 0.625f, 0.788f, 0.255f, 0.413f },
    { 0.750f, 0.928f, 0.383f, 0.324f }, { 0.875f, 0.983f, 0.601f, 0.372f },
    { 1.000f, 0.987f, 0.991f, 0.750f }
};

/** Blue-white-red. For baseline-normalised data, where the midpoint means "no
    change" and the sign carries meaning. */
constexpr ControlPoint diverging[] = {
    { 0.000f, 0.129f, 0.290f, 0.529f }, { 0.250f, 0.404f, 0.596f, 0.769f },
    { 0.500f, 0.969f, 0.969f, 0.969f }, { 0.750f, 0.839f, 0.376f, 0.302f },
    { 1.000f, 0.647f, 0.058f, 0.082f }
};

constexpr ControlPoint greyscale[] = { { 0.000f, 0.0f, 0.0f, 0.0f },
                                       { 1.000f, 1.0f, 1.0f, 1.0f } };

juce::uint32 packArgb (float r, float g, float b)
{
    const auto toByte = [] (float value)
    { return static_cast<juce::uint32> (std::clamp (value, 0.0f, 1.0f) * 255.0f + 0.5f); };

    return 0xff000000u | (toByte (r) << 16) | (toByte (g) << 8) | toByte (b);
}

void buildTable (std::span<const ControlPoint> points, std::span<juce::uint32> table)
{
    for (std::size_t i = 0; i < table.size(); ++i)
    {
        const float position = static_cast<float> (i) / static_cast<float> (table.size() - 1);

        // Find the bracketing control points and interpolate linearly between
        // them. The control points are dense enough that linear RGB
        // interpolation stays perceptually smooth.
        std::size_t upper = 1;
        while (upper < points.size() - 1 && points[upper].position < position)
            ++upper;

        const auto& a = points[upper - 1];
        const auto& b = points[upper];

        const float span = b.position - a.position;
        const float t = span > 0.0f ? (position - a.position) / span : 0.0f;

        table[i] = packArgb (a.red + t * (b.red - a.red),
                             a.green + t * (b.green - a.green),
                             a.blue + t * (b.blue - a.blue));
    }
}

} // namespace

ColorMap::ColorMap (ColorMapType type) : m_type (type) { rebuild(); }

void ColorMap::setType (ColorMapType type)
{
    if (type == m_type)
        return;

    m_type = type;
    rebuild();
}

void ColorMap::rebuild()
{
    switch (m_type)
    {
        case ColorMapType::Magma:
            buildTable (magma, m_table);
            break;
        case ColorMapType::Diverging:
            buildTable (diverging, m_table);
            break;
        case ColorMapType::Greyscale:
            buildTable (greyscale, m_table);
            break;
        case ColorMapType::Viridis:
        default:
            buildTable (viridis, m_table);
            break;
    }
}

juce::uint32 ColorMap::lookupArgb (float normalised) const
{
    if (! std::isfinite (normalised))
        return m_table[0];

    const int index = static_cast<int> (std::clamp (normalised, 0.0f, 1.0f) * (lookupTableSize - 1)
                                        + 0.5f);

    return m_table[static_cast<std::size_t> (std::clamp (index, 0, lookupTableSize - 1))];
}

juce::Colour ColorMap::lookup (float normalised) const
{
    return juce::Colour (lookupArgb (normalised));
}

juce::String ColorMap::getName (ColorMapType type)
{
    switch (type)
    {
        case ColorMapType::Magma:
            return "Magma";
        case ColorMapType::Diverging:
            return "Blue-Red";
        case ColorMapType::Greyscale:
            return "Greyscale";
        case ColorMapType::Viridis:
        default:
            return "Viridis";
    }
}

void ColorMap::drawColourBar (juce::Graphics& g, juce::Rectangle<int> bounds, bool vertical) const
{
    if (bounds.isEmpty())
        return;

    const int steps = vertical ? bounds.getHeight() : bounds.getWidth();

    for (int i = 0; i < steps; ++i)
    {
        // Vertical bars run low at the bottom, which is how an axis reads.
        const float position =
            vertical ? 1.0f - static_cast<float> (i) / static_cast<float> (steps - 1)
                     : static_cast<float> (i) / static_cast<float> (steps - 1);

        g.setColour (lookup (position));

        if (vertical)
            g.fillRect (bounds.getX(), bounds.getY() + i, bounds.getWidth(), 1);
        else
            g.fillRect (bounds.getX() + i, bounds.getY(), 1, bounds.getHeight());
    }
}

juce::Image buildSpectrogramImage (std::span<const float> values,
                                   int numFrequencies,
                                   int numBins,
                                   float minValue,
                                   float maxValue,
                                   const ColorMap& colourMap)
{
    if (numFrequencies <= 0 || numBins <= 0
        || values.size() < static_cast<std::size_t> (numFrequencies) * numBins)
        return {};

    juce::Image image (juce::Image::ARGB, numBins, numFrequencies, false);

    const float range = maxValue - minValue;
    const float inverseRange = (std::abs (range) > 0.0f) ? 1.0f / range : 0.0f;

    // Writing through a BitmapData in one pass is what keeps this cheap enough to
    // run on every committed trial; per-cell fillRect is roughly an order of
    // magnitude slower at these sizes.
    const juce::Image::BitmapData bitmap (image, juce::Image::BitmapData::writeOnly);

    for (int f = 0; f < numFrequencies; ++f)
    {
        // Row 0 of the image is the highest frequency: frequency increases
        // upwards on screen, but image rows increase downwards.
        const int row = numFrequencies - 1 - f;
        auto* destination = reinterpret_cast<juce::uint32*> (bitmap.getLinePointer (row));

        const float* source = values.data() + static_cast<std::size_t> (f) * numBins;

        for (int bin = 0; bin < numBins; ++bin)
            destination[bin] = colourMap.lookupArgb ((source[bin] - minValue) * inverseRange);
    }

    return image;
}

} // namespace TriggeredSpectra
