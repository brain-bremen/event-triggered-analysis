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
#include "SessionIoThread.h"

namespace EventTriggered
{

SessionIoThread::SessionIoThread() : juce::Thread ("Session I/O")
{
    // Below the capture worker and far below audio: a session save must never
    // take cycles from the acquisition it is saving.
    startThread (juce::Thread::Priority::low);
}

SessionIoThread::~SessionIoThread()
{
    signalThreadShouldExit();
    m_wakeUp.signal();

    // Generous, because the wait is for a disk write that is already in flight;
    // killing it half way through would leave a staging directory behind. The
    // staging directory is what makes that survivable, but waiting is better.
    stopThread (5000);

    m_completion.cancelPendingUpdate();
}

bool SessionIoThread::isBusy() const
{
    const std::lock_guard<std::mutex> lock (m_mutex);
    return m_job != Job::None;
}

bool SessionIoThread::save (std::unique_ptr<SessionWriter> writer, const juce::File& directory)
{
    if (writer == nullptr)
        return false;

    {
        const std::lock_guard<std::mutex> lock (m_mutex);

        if (m_job != Job::None)
            return false;

        m_job = Job::Save;
        m_directory = directory;
        m_writer = std::move (writer);
    }

    m_wakeUp.signal();
    return true;
}

bool SessionIoThread::load (const juce::File& directory)
{
    {
        const std::lock_guard<std::mutex> lock (m_mutex);

        if (m_job != Job::None)
            return false;

        m_job = Job::Load;
        m_directory = directory;
    }

    m_wakeUp.signal();
    return true;
}

void SessionIoThread::run()
{
    while (! threadShouldExit())
    {
        m_wakeUp.wait (-1);
        m_wakeUp.reset();

        if (threadShouldExit())
            return;

        Job job = Job::None;
        juce::File directory;
        std::unique_ptr<SessionWriter> writer;

        {
            const std::lock_guard<std::mutex> lock (m_mutex);
            job = m_job;
            directory = m_directory;
            writer = std::move (m_writer);
        }

        if (job == Job::None)
            continue;

        // The only part of the whole feature that touches the disk. No lock is
        // held here — not this class's, and not the DataStore's.
        if (job == Job::Save)
        {
            const auto startMs = juce::Time::getMillisecondCounterHiRes();

            SaveResult result;
            result.directory = directory;
            result.bytesWritten = writer->getEncodedSize();
            result.result = writer->flushToDirectory (directory);
            result.elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - startMs) * 0.001;

            const std::lock_guard<std::mutex> lock (m_mutex);
            m_saveResult = std::move (result);
            m_finishedJob = Job::Save;
        }
        else
        {
            LoadResult result;
            result.directory = directory;

            auto reader = std::make_unique<SessionReader> (directory);

            if (! reader->isValid())
            {
                result.error = reader->getError();
            }
            else if (! reader->preloadArrays())
            {
                result.error = "Some arrays listed in the manifest could not be read";
            }
            else
            {
                result.reader = std::move (reader);
            }

            const std::lock_guard<std::mutex> lock (m_mutex);
            m_loadResult = std::move (result);
            m_finishedJob = Job::Load;
        }

        // The job stays marked as running until the result has been delivered,
        // so isBusy() does not go false while a completion is still in flight and
        // let a second request overwrite the result of the first.
        m_completion.triggerAsyncUpdate();
    }
}

void SessionIoThread::deliverResult()
{
    Job finished = Job::None;
    SaveResult saveResult;
    LoadResult loadResult;

    {
        const std::lock_guard<std::mutex> lock (m_mutex);

        finished = m_finishedJob;
        m_finishedJob = Job::None;

        if (finished == Job::Save)
            saveResult = std::move (m_saveResult);
        else if (finished == Job::Load)
            loadResult = std::move (m_loadResult);

        // Cleared before the callback runs, so a callback that immediately
        // queues another job is accepted rather than refused as busy.
        m_job = Job::None;
        m_directory = juce::File();
    }

    if (finished == Job::Save && onSaveFinished != nullptr)
        onSaveFinished (std::move (saveResult));
    else if (finished == Job::Load && onLoadFinished != nullptr)
        onLoadFinished (std::move (loadResult));
}

} // namespace EventTriggered
