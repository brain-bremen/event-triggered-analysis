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

#include "../SweepAngles.h"

#include <JuceHeader.h>
#include <VisualizerEditorHeaders.h>
#include <functional>
#include <vector>

namespace EventTriggered
{

class BarMapperNode;
class TriggerSource;

/** N arrows at the configured angles, labelled.
 *
 *  The whole point of the stimulus window. The angle assigned to each condition
 *  is the one thing in this plugin that nothing can verify: swap two and the map
 *  is wrong with no error anywhere. A picture of the directions makes a typo or a
 *  duplicate obvious at a glance, before the recording rather than after it, and
 *  it redraws when the convention changes — which is what turns an invisible
 *  180-degree mistake into something you can see.
 */
class CompassPreview : public juce::Component
{
public:
    struct Arrow
    {
        double canonicalAngleDeg = 0.0;
        juce::String label;
        juce::Colour colour;
    };

    void setArrows (std::vector<Arrow> arrows);
    void paint (juce::Graphics& g) override;

private:
    std::vector<Arrow> m_arrows;
};

/** One arrow per trigger source that has an angle, in the node's current
 *  convention. Shared by RfAnalysisSettingsWindow's inline compass and
 *  SweepDirectionsPanel, so the two never compute it two different ways. */
std::vector<CompassPreview::Arrow> buildCompassArrows (BarMapperNode& node);

/** The angle-set warnings (missing angles, duplicates, ...), joined into one
 *  line, or empty if there are none. Same sharing reason as buildCompassArrows. */
juce::String describeAngleWarnings (BarMapperNode& node);

/** The angle table and the direction generator, opened from a button beside
 *  RfAnalysisSettingsWindow's compass rather than shown inline -- the table,
 *  the convention selectors and the generator's five controls do not fit next
 *  to the mapping parameters without crowding them. Launched as a nested
 *  call-out (see NestedCallOut.h); a fresh instance each time, so it always
 *  opens showing the node's current state. */
class SweepDirectionsPanel : public juce::Component,
                             public juce::Button::Listener,
                             public juce::Label::Listener,
                             public juce::ComboBox::Listener
{
public:
    SweepDirectionsPanel (BarMapperNode* node, bool acquisitionIsActive);
    ~SweepDirectionsPanel() override;

    /** Fires whenever an edit here could have changed what the compass or the
        warning line outside this popout should show -- an angle typed in, the
        convention changed, or REPLACE regenerating the sources. The popout
        does not own either, so it reports the change instead of drawing it. */
    std::function<void()> onChanged;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void buttonClicked (juce::Button* button) override;
    void labelTextChanged (juce::Label* label) override;
    void comboBoxChanged (juce::ComboBox* box) override;

private:
    void refresh();
    void rebuildRows();
    void refreshWarnings();
    void applyGeneratedDirections();

    /** The generator settings as the controls currently read. */
    DirectionGeneratorSpec specFromControls() const;

    /** Pushes the controls into the node and redraws the preview line.
     *
     *  The settings are stored on every edit rather than on REPLACE, so a message
     *  form typed once is saved with the signal chain even if no set is generated
     *  in this session. */
    void generatorSettingsChanged();

    /** Fills the generator controls from the node's stored settings. */
    void syncGeneratorControls();

    /** One row of the angle table: which condition, and what direction it means. */
    struct Row
    {
        TriggerSource* source = nullptr;
        std::unique_ptr<juce::Label> name;
        std::unique_ptr<juce::Label> armPattern;
        std::unique_ptr<juce::Label> angle;
    };

    BarMapperNode* m_node = nullptr;
    bool m_acquisitionIsActive = false;

    std::vector<Row> m_rows;

    std::unique_ptr<juce::Label> m_conventionLabel;
    std::unique_ptr<juce::ComboBox> m_zeroSelector;
    std::unique_ptr<juce::ComboBox> m_senseSelector;

    // --- The generator ------------------------------------------------------

    std::unique_ptr<juce::Label> m_generateLabel;
    std::unique_ptr<juce::ComboBox> m_generateCount;
    std::unique_ptr<UtilityButton> m_generateButton;

    std::unique_ptr<juce::Label> m_triggerLabel;
    std::unique_ptr<juce::Label> m_triggerNumber;
    std::unique_ptr<juce::ToggleButton> m_incrementTrigger;

    std::unique_ptr<juce::Label> m_armLabel;
    std::unique_ptr<juce::Label> m_armBase;
    std::unique_ptr<juce::Label> m_armNumber;
    std::unique_ptr<juce::Label> m_armSuffix;

    /** The first and last arm pattern the current settings would produce.
     *
     *  The generator writes patterns the user never types, against messages this
     *  plugin cannot see. Showing the two ends of the range is what turns "it
     *  never fires" from a debugging session into a misspelling you can read. */
    std::unique_ptr<juce::Label> m_previewLabel;

    std::unique_ptr<juce::Label> m_warningLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SweepDirectionsPanel)
};

} // namespace EventTriggered
