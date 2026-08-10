/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI Plugin Triggered Average
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
#include <EditorHeaders.h>
#include <VisualizerEditorHeaders.h>
class Visualizer;

namespace TriggeredAverage
{
class TriggeredAvgCanvas;
class TriggeredAvgNode;
class TriggerSource;
enum class TriggerType : std::int_fast8_t;

namespace Popup
{
    class PopupConfigurationWindow;
}

class TriggeredAvgEditor : public VisualizerEditor, public Button::Listener
{
public:
    TriggeredAvgEditor (GenericProcessor* parentNode);
    ~TriggeredAvgEditor() override = default;

    /** Creates the visualizer */
    Visualizer* createNewCanvas() override;

    /** Called when signal chain is updated */
    void updateSettings() override;

    /** Called when source colours are updated */
    void updateColours (TriggerSource*);

    /** Called when condition name is updated */
    void updateConditionName (TriggerSource*);

    /** Called when configure button is clicked */
    void buttonClicked (Button* button) override;

    /** Adds triggers with a given type */
    void addTriggerSources (Popup::PopupConfigurationWindow* window,
                            Array<int> lines,
                            TriggerType type) const;

    /** Removes triggers based on an array of pointers to trigger objects*/
    void removeTriggerSources (Popup::PopupConfigurationWindow* window,
                               Array<TriggerSource*> triggerSourcesToRemove) const;

private:
    std::unique_ptr<UtilityButton> configureButton;

    TriggeredAvgCanvas* canvas;

    Popup::PopupConfigurationWindow* currentConfigWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggeredAvgEditor);
};

} // namespace TriggeredAverage
