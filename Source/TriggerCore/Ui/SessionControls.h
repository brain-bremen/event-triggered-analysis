/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI plugins TriggeredAverage,
    TriggeredPower, TriggeredCoherence and ReceptiveFieldBarMapper.
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

#include "../TriggeredCaptureNode.h"

#include <VisualizerWindowHeaders.h>

namespace EventTriggered
{

/** SAVE and LOAD buttons for a plugin's accumulated data.
 *
 *  One component rather than a pair of buttons in each canvas, because the
 *  behaviour around them is the part worth getting right once: which operations
 *  are legal during acquisition, what a refused load has to tell the user, and
 *  the fact that neither button may block the message thread on a disk.
 *
 *  Drop it into an options bar and give it getDesiredWidth() pixels.
 *
 *  ### What the buttons are allowed to do
 *
 *  SAVE works during acquisition. The node copies its accumulators under their
 *  own lock and hands the copy to the session I/O thread, so saving mid-run costs
 *  the capture worker one memcpy and nothing else. That is deliberate: a save you
 *  have to stop the experiment for is a save nobody makes.
 *
 *  LOAD does not, and the button disables itself while acquiring. Restoring
 *  accumulators replaces the buffers the capture worker is writing into, and
 *  there is no version of that which is safe to do underneath a running
 *  acquisition. The node refuses it independently; this is only the affordance.
 */
class SessionControls : public juce::Component,
                        public juce::Button::Listener,
                        private juce::Timer
{
public:
    explicit SessionControls (TriggeredCaptureNode* node);
    ~SessionControls() override;

    void buttonClicked (juce::Button* button) override;
    void resized() override;

    /** Width this needs in an options bar. */
    int getDesiredWidth() const;

private:
    /** Keeps LOAD's enabled state in step with acquisition.
     *
     *  Polled rather than pushed because acquisition starting is not an event
     *  this component is offered: startAcquisition() belongs to the node, and
     *  routing a notification through it for one button's enablement would put a
     *  UI concern in the audio path's way. Twice a second is far more often than
     *  a person can act on it. */
    void timerCallback() override;

    void saveSession();
    void loadSession();

    /** A directory name a person can find again: the plugin, then when it was
        saved, in an order that sorts chronologically. */
    juce::String suggestedDirectoryName() const;

    TriggeredCaptureNode* m_node;

    std::unique_ptr<UtilityButton> m_saveButton;
    std::unique_ptr<UtilityButton> m_loadButton;

    bool m_wasAcquiring = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionControls)
};

} // namespace EventTriggered
