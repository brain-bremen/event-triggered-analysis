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

#include "SessionBundle.h"

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <mutex>

namespace EventTriggered
{

/** Moves session reads and writes off the threads that must not block.
 *
 *  A session is tens of megabytes of accumulators and a handful of PNGs. Writing
 *  that takes as long as the disk takes, which on a busy recording rig is not
 *  bounded by anything useful. Two threads must never wait for it:
 *
 *    - the **audio thread**, which is appending to the ring buffer and pushing
 *      capture requests. It never touches a session directly, but it does
 *      contend for the DataStore lock with whatever gathers the accumulators —
 *      so the gather must copy and get out, and the writing must happen after
 *      the lock is released. That is why SessionWriter builds the whole bundle in
 *      memory and this class only ever calls flushToDirectory().
 *
 *    - the **message thread**, which is drawing the GUI. A save that froze the
 *      editor for a second every time would be a save nobody uses during an
 *      experiment.
 *
 *  So the division of labour is fixed:
 *
 *      message thread    gather under the lock, encode, hand over    (fast)
 *      this thread       touch the disk                              (slow)
 *      message thread    apply the result, repaint                   (fast)
 *
 *  Loading is split the same way round: this thread reads and parses the whole
 *  directory into memory (SessionReader::preloadArrays), and the caller applies
 *  it on the message thread with acquisition stopped, where it is pure memcpy.
 *
 *  One job at a time. A second request while one is running is refused rather
 *  than queued: the only way to issue one is to press a button, and a user who
 *  presses Save twice means "save", not "save twice".
 */
class SessionIoThread : private juce::Thread
{
public:
    struct SaveResult
    {
        juce::File directory;
        juce::Result result = juce::Result::ok();
        std::int64_t bytesWritten = 0;
        double elapsedSeconds = 0.0;
    };

    struct LoadResult
    {
        juce::File directory;

        /** Null when the session could not be opened; `error` says why.
            Its arrays are already in memory, so reading them costs no I/O. */
        std::unique_ptr<SessionReader> reader;

        juce::String error;

        bool wasOk() const { return reader != nullptr; }
    };

    SessionIoThread();
    ~SessionIoThread() override;

    /** Queues a save, taking ownership of the fully-built bundle.
     *
     *  The writer must be complete: nothing is added to it after this point, and
     *  it is touched only by the background thread from here on.
     *
     *  Returns false if a job is already running, in which case `writer` is left
     *  untouched and nothing is queued. */
    bool save (std::unique_ptr<SessionWriter> writer, const juce::File& directory);

    /** Queues a load, reading and parsing the whole directory into memory.
     *
     *  Returns false if a job is already running. */
    bool load (const juce::File& directory);

    /** True while a job is queued or running. */
    bool isBusy() const;

    /** Called on the message thread when a job finishes.
     *
     *  Assign before queueing anything. Both are invoked exactly once per
     *  accepted request, including on failure — a save that silently does not
     *  happen is worse than one that reports an error. */
    std::function<void (SaveResult)> onSaveFinished;
    std::function<void (LoadResult)> onLoadFinished;

private:
    void run() override;

    /** Marshals a finished job back to the message thread.
     *
     *  Its own AsyncUpdater rather than making the owner one, so that a node
     *  which already uses AsyncUpdater for its display refresh does not have to
     *  multiplex two unrelated meanings onto one handler. */
    struct Completion : public juce::AsyncUpdater
    {
        explicit Completion (SessionIoThread& owner) : m_owner (owner) {}
        void handleAsyncUpdate() override { m_owner.deliverResult(); }

        SessionIoThread& m_owner;
    };

    void deliverResult();

    Completion m_completion { *this };

    enum class Job
    {
        None,
        Save,
        Load
    };

    mutable std::mutex m_mutex;

    Job m_job = Job::None;
    juce::File m_directory;
    std::unique_ptr<SessionWriter> m_writer;

    /** Filled in by the worker, drained by deliverResult() on the message
        thread. */
    Job m_finishedJob = Job::None;
    SaveResult m_saveResult;
    LoadResult m_loadResult;

    juce::WaitableEvent m_wakeUp { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SessionIoThread)
};

} // namespace EventTriggered
