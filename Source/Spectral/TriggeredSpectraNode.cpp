/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
    Copyright (C) 2022 Open Ephys
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
#include "TriggeredSpectraNode.h"

#include "SpectralParameterNames.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace EventTriggered
{

namespace
{
    /** Padding is 3 sigma of the widest Morlet wavelet, i.e. the one at the lowest
    frequency, where sigma_t = nCycles / (2 pi f). Beyond 3 sigma the Gaussian
    envelope is below 1% and the edge contamination is negligible. */
    constexpr double paddingSigmas = 3.0;
} // namespace

TriggeredSpectraNode::TriggeredSpectraNode (const juce::String& name) : TriggeredCaptureNode (name)
{
}

TriggeredSpectraNode::~TriggeredSpectraNode() = default;

// --- Parameters ------------------------------------------------------------

void TriggeredSpectraNode::registerAdditionalParameters()
{
    using P = Parameter;

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             ParameterNames::mode,
                             "Mode",
                             "Time-resolved spectrogram, or one spectrum per trial window",
                             { "Spectrogram", "Spectrum" },
                             static_cast<int> (EstimateMode::Spectrogram),
                             true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::freq_min,
                       "Freq min",
                       "Lowest frequency to estimate",
                       "Hz",
                       2.0f,
                       0.1f,
                       1000.0f,
                       0.5f,
                       true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::freq_max,
                       "Freq max",
                       "Highest frequency to estimate",
                       "Hz",
                       200.0f,
                       1.0f,
                       10000.0f,
                       5.0f,
                       true);

    addIntParameter (P::PROCESSOR_SCOPE,
                     ParameterNames::num_freqs,
                     "Num freqs",
                     "Number of frequencies in the spectrogram",
                     60,
                     4,
                     512,
                     true);

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             ParameterNames::freq_spacing,
                             "Spacing",
                             "Spacing of the spectrogram frequency grid",
                             { "Linear", "Log" },
                             1,
                             true);

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             ParameterNames::tf_method,
                             "TF method",
                             "Estimator used in spectrogram mode",
                             { "Morlet", "Hann STFT" },
                             0,
                             true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::n_cycles_low,
                       "Cycles low",
                       "Morlet cycles at the lowest frequency",
                       "",
                       3.0f,
                       1.0f,
                       20.0f,
                       0.5f,
                       true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::n_cycles_high,
                       "Cycles high",
                       "Morlet cycles at the highest frequency",
                       "",
                       10.0f,
                       1.0f,
                       40.0f,
                       0.5f,
                       true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::stft_window_ms,
                       "STFT window",
                       "Sliding window length in Hann STFT mode",
                       "ms",
                       256.0f,
                       16.0f,
                       4000.0f,
                       16.0f,
                       true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::stft_hop_ms,
                       "STFT hop",
                       "Step between successive STFT windows",
                       "ms",
                       25.0f,
                       1.0f,
                       1000.0f,
                       1.0f,
                       true);

    addCategoricalParameter (P::PROCESSOR_SCOPE,
                             ParameterNames::line_method,
                             "Line method",
                             "Estimator used in spectrum mode",
                             { "Multitaper", "Hann" },
                             0,
                             true);

    addFloatParameter (P::PROCESSOR_SCOPE,
                       ParameterNames::nw,
                       "NW",
                       "Time-bandwidth product for the DPSS tapers",
                       "",
                       3.0f,
                       1.0f,
                       10.0f,
                       0.5f,
                       true);

    addIntParameter (P::PROCESSOR_SCOPE,
                     ParameterNames::n_tapers,
                     "Tapers",
                     "Number of DPSS tapers; 2*NW-1 is the usual choice",
                     5,
                     1,
                     19,
                     true);

    registerPluginParameters();
}

EstimateMode TriggeredSpectraNode::getEstimateMode() const
{
    auto* parameter = getParameter (ParameterNames::mode);

    if (parameter == nullptr)
        return EstimateMode::Spectrogram;

    const int index = static_cast<CategoricalParameter*> (parameter)->getSelectedIndex();
    return static_cast<EstimateMode> (index);
}

bool TriggeredSpectraNode::isAnalysisParameter (const juce::String& parameterName) const
{
    static const juce::StringArray analysisParameters {
        ParameterNames::mode,         ParameterNames::freq_min,      ParameterNames::freq_max,
        ParameterNames::num_freqs,    ParameterNames::freq_spacing,  ParameterNames::tf_method,
        ParameterNames::n_cycles_low, ParameterNames::n_cycles_high, ParameterNames::stft_window_ms,
        ParameterNames::stft_hop_ms,  ParameterNames::line_method,   ParameterNames::nw,
        ParameterNames::n_tapers
    };

    if (analysisParameters.contains (parameterName, true))
        return true;

    return TriggeredCaptureNode::isAnalysisParameter (parameterName);
}

int TriggeredSpectraNode::computePadSamples (float sampleRate) const
{
    // Only wavelets need padding; a periodogram or STFT is computed on exactly
    // the samples it is given.
    if (getEstimateMode() != EstimateMode::Spectrogram
        || getParameter (ParameterNames::tf_method) == nullptr
        || static_cast<CategoricalParameter*> (getParameter (ParameterNames::tf_method))
                   ->getSelectedIndex()
               != 0)
        return 0;

    const double lowestFrequency =
        std::max (0.1, static_cast<double> (getParameter (ParameterNames::freq_min)->getValue()));
    const double cyclesAtLowest = std::max (
        1.0, static_cast<double> (getParameter (ParameterNames::n_cycles_low)->getValue()));

    const double sigmaSeconds = cyclesAtLowest / (2.0 * std::numbers::pi * lowestFrequency);

    return static_cast<int> (std::ceil (paddingSigmas * sigmaSeconds * sampleRate));
}

} // namespace EventTriggered
