/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredPower and
    TriggeredCoherence.
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

#include "ParameterControl.h"

#include <JuceHeader.h>
#include <ProcessorHeaders.h>
#include <VisualizerEditorHeaders.h>
#include <memory>
#include <vector>

namespace TriggeredSpectra
{

class TriggeredSpectraNode;

/** Every parameter that changes what is computed, in one place.
 *
 *  The split is the one the editor implies: anything affecting data collection or
 *  the estimate itself lives here, reached from the editor; anything affecting
 *  only how the result is drawn lives on the canvas, next to the plot it changes.
 *  Nineteen parameters do not fit in a 220 px editor, so the most-used four
 *  (channels, pre, post, mode) stay on the editor and the rest are here.
 *
 *  Two things this window has to get right, because getting them wrong is
 *  destructive rather than merely untidy:
 *
 *  - **Every parameter here is deactivateDuringAcquisition**, and
 *    rebuildConfiguration() asserts that acquisition is stopped before it
 *    reallocates the ring buffer under the audio thread. The whole window is
 *    therefore disabled while running, and says so.
 *  - **Changing any of these discards accumulated spectra**, since they are what
 *    the accumulators are shaped by. That is stated in the window rather than
 *    discovered.
 *
 *  Sections are greyed out when the current mode makes them irrelevant — Morlet
 *  settings in Spectrum mode, taper settings in Spectrogram mode — rather than
 *  hidden, so the window also documents which estimator is actually running.
 */
class AnalysisSettingsWindow : public PopupComponent
{
public:
    /** @param anchor  the component the popup is shown from. PopupComponent
                       dereferences it in its constructor, so it must not be null. */
    AnalysisSettingsWindow (TriggeredSpectraNode* node,
                            bool acquisitionIsActive,
                            juce::Component* anchor);
    ~AnalysisSettingsWindow() override;

    void updatePopup() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    /** A titled group of controls, enabled or greyed as a unit. */
    struct Section
    {
        juce::String title;

        /** Shown beside the title when the section is greyed, naming what would
            have to change for it to apply. */
        juce::String inactiveNote;

        std::vector<std::unique_ptr<ParameterControl>> controls;

        bool active = true;
        juce::Rectangle<int> bounds;
    };

    /** Builds a section from parameter names, skipping any the node does not have
        — max_trials exists only on TriggeredPower. */
    Section* addSection (const juce::String& title,
                         const juce::String& inactiveNote,
                         const std::vector<const char*>& parameterNames);

    /** Re-reads every control from its parameter. */
    void refreshValues();

    /** Greys the sections the current mode does not use, and everything at all
        while acquisition is running. */
    void applyEnablement();

    /** Height the sections need, used to size the window. */
    int layoutSections (int width, bool applyBounds);

    bool isSpectrogramMode() const;
    int selectedIndexOf (const char* parameterName) const;

    static constexpr int rowHeight = 26;
    static constexpr int sectionTitleHeight = 24;
    static constexpr int sectionGap = 10;
    static constexpr int columnWidth = 300;
    static constexpr int nameWidth = 104;
    static constexpr int controlWidth = 116;
    static constexpr int unitWidth = 40;
    static constexpr int headerHeight = 34;
    static constexpr int footerHeight = 30;

    TriggeredSpectraNode* m_node = nullptr;
    bool m_acquisitionIsActive = false;

    std::vector<std::unique_ptr<Section>> m_sections;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalysisSettingsWindow)
};

} // namespace TriggeredSpectra
