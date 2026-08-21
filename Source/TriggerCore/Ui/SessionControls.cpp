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
#include "SessionControls.h"

namespace EventTriggered
{

namespace
{
    constexpr int buttonWidth = 54;
    constexpr int buttonGap = 4;
} // namespace

SessionControls::SessionControls (TriggeredCaptureNode* node) : m_node (node)
{
    m_saveButton = std::make_unique<UtilityButton> ("SAVE");
    m_saveButton->setClickingTogglesState (false);
    m_saveButton->setTooltip ("Save the accumulated data, settings and figures to a folder");
    m_saveButton->addListener (this);
    addAndMakeVisible (m_saveButton.get());

    m_loadButton = std::make_unique<UtilityButton> ("LOAD");
    m_loadButton->setClickingTogglesState (false);
    m_loadButton->setTooltip ("Load a saved session and carry on from where it left off");
    m_loadButton->addListener (this);
    addAndMakeVisible (m_loadButton.get());

    if (m_node != nullptr)
    {
        // Both fire on the message thread; see SessionIoThread.
        m_node->onSessionSaved = [this] (juce::Result result)
        {
            if (result.wasOk())
                CoreServices::sendStatusMessage ("Session saved.");
            else
                CoreServices::sendStatusMessage ("Could not save session: "
                                                 + result.getErrorMessage());
        };

        m_node->onSessionLoaded = [this] (SessionCompatibility report, bool applied)
        {
            if (applied)
            {
                CoreServices::sendStatusMessage (
                    report.verdict == SessionVerdict::Rebuild
                        ? "Session loaded; trigger sources were created from the file."
                        : "Session loaded.");
                return;
            }

            // A refusal gets a dialog rather than the status bar. The status bar
            // is a single line that scrolls away, and the reasons are the whole
            // value of refusing: "different channel count" is something the user
            // can fix in seconds once they are told, and cannot guess otherwise.
            juce::AlertWindow::showMessageBoxAsync (
                juce::AlertWindow::WarningIcon,
                "Session not loaded",
                report.problems.isEmpty()
                    ? "The session could not be read."
                    : "This session does not match the current configuration:\n\n  "
                          + report.problems.joinIntoString ("\n  "));
        };
    }

    startTimer (500);
}

SessionControls::~SessionControls()
{
    stopTimer();

    // The callbacks capture `this`. Clearing them is what stops the node calling
    // into a destroyed component when a job finishes after the visualizer has
    // been closed -- which is exactly when a long save completes.
    if (m_node != nullptr)
    {
        m_node->onSessionSaved = nullptr;
        m_node->onSessionLoaded = nullptr;
    }
}

int SessionControls::getDesiredWidth() const { return 2 * buttonWidth + buttonGap; }

void SessionControls::resized()
{
    auto area = getLocalBounds();

    m_saveButton->setBounds (area.removeFromLeft (buttonWidth));
    area.removeFromLeft (buttonGap);
    m_loadButton->setBounds (area.removeFromLeft (buttonWidth));
}

void SessionControls::timerCallback()
{
    const bool acquiring = CoreServices::getAcquisitionStatus();

    if (acquiring == m_wasAcquiring)
        return;

    m_wasAcquiring = acquiring;
    m_loadButton->setEnabled (! acquiring);
}

juce::String SessionControls::suggestedDirectoryName() const
{
    const auto name = m_node != nullptr ? m_node->getName().replaceCharacter (' ', '_')
                                        : juce::String ("session");

    return name + "_" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H-%M-%S");
}

void SessionControls::buttonClicked (juce::Button* button)
{
    if (m_node == nullptr)
        return;

    if (button == m_saveButton.get())
        saveSession();
    else if (button == m_loadButton.get())
        loadSession();
}

void SessionControls::saveSession()
{
    if (m_node->isSessionIoBusy())
    {
        CoreServices::sendStatusMessage ("A session is still being written; try again shortly.");
        return;
    }

    juce::FileChooser chooser (
        "Save session as...",
        CoreServices::getDefaultUserSaveDirectory().getChildFile (suggestedDirectoryName()),
        "",
        true);

    if (! chooser.browseForFileToSave (true))
        return;

    // Returns false only for a state that cannot produce a session at all -- no
    // trigger sources, no valid trial window, or a plugin that does not implement
    // the payload. Saying which of those it is here would mean duplicating the
    // node's own conditions; saying that nothing was written is the part the user
    // needs.
    if (! m_node->saveSession (chooser.getResult()))
        CoreServices::sendStatusMessage (
            "Nothing to save: configure trigger sources and record some trials first.");
    else
        CoreServices::sendStatusMessage ("Saving session...");
}

void SessionControls::loadSession()
{
    if (CoreServices::getAcquisitionStatus())
    {
        CoreServices::sendStatusMessage ("Stop acquisition before loading a session.");
        return;
    }

    if (m_node->isSessionIoBusy())
    {
        CoreServices::sendStatusMessage ("A session is still being read; try again shortly.");
        return;
    }

    juce::FileChooser chooser (
        "Choose a session folder...", CoreServices::getDefaultUserSaveDirectory(), "", true);

    if (! chooser.browseForDirectory())
        return;

    if (! m_node->loadSession (chooser.getResult()))
        CoreServices::sendStatusMessage ("That folder is not a saved session.");
}

} // namespace EventTriggered
