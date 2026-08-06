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

#include <JuceHeader.h>

namespace TriggeredSpectra
{

/** Available colour scales.
 *
 *  Viridis and Magma are perceptually uniform: equal steps in value look like
 *  equal steps in brightness, and they survive greyscale printing and the common
 *  forms of colour blindness. Jet does not, which is why it is not offered.
 *
 *  Diverging is for baseline-normalised data, where zero is meaningful and the
 *  sign matters; it is symmetric about the midpoint.
 */
enum class ColorMapType
{
    Viridis = 0,
    Magma = 1,
    /** Blue-white-red, for signed data such as dB change from baseline. */
    Diverging = 2,
    Greyscale = 3
};

/** Maps a normalised value in [0, 1] to a colour, via a precomputed lookup table.
 *
 *  The LUT exists because a spectrogram repaint evaluates this once per pixel;
 *  interpolating control points per pixel showed up in profiling of the reference
 *  plugin's equivalent path.
 */
class ColorMap
{
public:
    explicit ColorMap (ColorMapType type = ColorMapType::Viridis);

    void setType (ColorMapType type);
    ColorMapType getType() const noexcept { return m_type; }

    /** Colour for a normalised value. Values outside [0, 1] clamp to the ends. */
    juce::Colour lookup (float normalised) const;

    /** Packed 0xAARRGGBB, for writing straight into an Image's pixel data. */
    juce::uint32 lookupArgb (float normalised) const;

    static juce::String getName (ColorMapType type);

    /** Renders the scale into a rectangle, for a colour-bar legend. */
    void drawColourBar (juce::Graphics& g, juce::Rectangle<int> bounds, bool vertical) const;

private:
    void rebuild();

    static constexpr int lookupTableSize = 256;

    ColorMapType m_type = ColorMapType::Viridis;
    std::array<juce::uint32, lookupTableSize> m_table {};
};

/** Builds an ARGB image from a value grid, one pixel per (frequency, bin).
 *
 *  Rendering into an Image and letting JUCE scale it is far cheaper than drawing
 *  per-cell rectangles, and it is what keeps a 60 x 512 spectrogram repaint cheap
 *  enough to run on every committed trial.
 *
 *  Row 0 of the image is the *highest* frequency, matching the convention that
 *  frequency increases upwards on screen.
 *
 *  @param values      numFrequencies * numBins, frequency-major
 *  @param minValue    mapped to the bottom of the scale
 *  @param maxValue    mapped to the top; a degenerate range renders flat
 */
juce::Image buildSpectrogramImage (std::span<const float> values,
                                   int numFrequencies,
                                   int numBins,
                                   float minValue,
                                   float maxValue,
                                   const ColorMap& colourMap);

} // namespace TriggeredSpectra
