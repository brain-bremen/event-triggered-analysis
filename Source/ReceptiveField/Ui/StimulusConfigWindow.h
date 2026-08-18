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

#include "RfMath/AngleConvention.h"

#include <JuceHeader.h>
#include <VisualizerEditorHeaders.h>
#include <vector>

namespace EventTriggered
{

class ReceptiveFieldNode;
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

/** The angle table, the compass, and the direction generator. */
class StimulusConfigWindow : public PopupComponent,
                             public juce::Button::Listener,
                             public juce::Label::Listener,
                             public juce::ComboBox::Listener
{
public:
    StimulusConfigWindow (ReceptiveFieldNode* node, bool acquisitionIsActive, juce::Component* anchor);
    ~StimulusConfigWindow() override;

    void updatePopup() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    void buttonClicked (juce::Button* button) override;
    void labelTextChanged (juce::Label* label) override;
    void comboBoxChanged (juce::ComboBox* box) override;

private:
    void rebuildRows();
    void refreshCompass();
    void applyGeneratedDirections();

    /** One row of the angle table: which condition, and what direction it means. */
    struct Row
    {
        TriggerSource* source = nullptr;
        std::unique_ptr<juce::Label> name;
        std::unique_ptr<juce::Label> armPattern;
        std::unique_ptr<juce::Label> angle;
    };

    ReceptiveFieldNode* m_node = nullptr;
    bool m_acquisitionIsActive = false;

    std::vector<Row> m_rows;

    std::unique_ptr<juce::Label> m_conventionLabel;
    std::unique_ptr<juce::ComboBox> m_zeroSelector;
    std::unique_ptr<juce::ComboBox> m_senseSelector;

    std::unique_ptr<juce::Label> m_generateLabel;
    std::unique_ptr<juce::ComboBox> m_generateCount;
    std::unique_ptr<UtilityButton> m_generateButton;

    std::unique_ptr<CompassPreview> m_compass;
    std::unique_ptr<juce::Label> m_warningLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StimulusConfigWindow)
};

} // namespace EventTriggered
