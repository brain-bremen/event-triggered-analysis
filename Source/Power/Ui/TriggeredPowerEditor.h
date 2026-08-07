/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugin TriggeredPower.
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

#include <EditorHeaders.h>
#include <JuceHeader.h>
#include <VisualizerEditorHeaders.h>

namespace TriggeredSpectra
{

class TriggeredPowerNode;
class TriggeredPowerCanvas;

class TriggeredPowerEditor : public VisualizerEditor, public juce::Button::Listener
{
public:
    explicit TriggeredPowerEditor (GenericProcessor* parentNode);
    ~TriggeredPowerEditor() override = default;

    Visualizer* createNewCanvas() override;

    void buttonClicked (juce::Button* button) override;

private:
    /** Opens the trigger-source table. Without at least one source nothing is
        ever captured, so this is the first thing a user needs. */
    std::unique_ptr<UtilityButton> m_configureButton;

    /** Opens every parameter that changes what is computed. Display-only settings
        deliberately live on the canvas instead. */
    std::unique_ptr<UtilityButton> m_analysisButton;

    /** Opens the live trigger counters, for when a source is configured but no
        trials appear. */
    std::unique_ptr<UtilityButton> m_monitorButton;

    TriggeredPowerCanvas* m_canvas = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredPowerEditor)
};

} // namespace TriggeredSpectra
