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
#pragma once

#include "TriggerCore/TriggeredCaptureNode.h"
#include "SpectralTypes.h"

#include <JuceHeader.h>

namespace TriggeredSpectra
{

/** What TriggeredPower and TriggeredCoherence have in common on top of
 *  TriggeredCaptureNode: the frequency-domain parameter set and the estimate mode.
 *
 *  Everything about *getting* trial windows — the ring buffer, the trigger
 *  sources, the work queue, the worker thread, the arm/cancel/commit workflow —
 *  lives in the base class and is shared with the time-domain averaging plugin.
 *  This class adds only what it means to be a *spectral* plugin.
 */
class TriggeredSpectraNode : public TriggeredCaptureNode
{
public:
    explicit TriggeredSpectraNode (const juce::String& name);
    ~TriggeredSpectraNode() override;

    EstimateMode getEstimateMode() const;

protected:
    /** Registers the frequency-domain parameter set, then hands over to
     *  registerPluginParameters().
     *
     *  `final` on purpose. A plugin that overrode this instead would shadow it
     *  and silently lose every frequency parameter — the plugin would load, the
     *  editor would come up, and the frequency controls would simply not be
     *  there. Sealing it turns that mistake into a compile error and leaves
     *  exactly one correct place to add parameters. */
    void registerAdditionalParameters() final;

    /** Registers the parameters belonging to one spectral plugin. Called after
     *  the frequency-domain set is in place. */
    virtual void registerPluginParameters() {}

    /** Extends the base set with every frequency-domain parameter. */
    bool isAnalysisParameter (const juce::String& parameterName) const override;

    /** Three sigma of the widest Morlet wavelet, or zero for any other estimator.
     *
     *  Only wavelets need padding: a periodogram and an STFT are computed on
     *  exactly the samples they are given. */
    int computePadSamples (float sampleRate) const override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredSpectraNode)
};

} // namespace TriggeredSpectra
