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
#include "TriggerCore/Session/SessionIoThread.h"

#include <JuceHeader.h>
#include <gtest/gtest.h>

#include <atomic>
#include <numeric>

using namespace EventTriggered;

namespace
{

class ScratchDirectory
{
public:
    ScratchDirectory()
        : m_directory (juce::File::getSpecialLocation (juce::File::tempDirectory)
                           .getChildFile ("oe_session_io_" + juce::Uuid().toDashedString()))
    {
        m_directory.createDirectory();
    }

    ~ScratchDirectory() { m_directory.deleteRecursively(); }

    juce::File child (const juce::String& name) const { return m_directory.getChildFile (name); }

private:
    juce::File m_directory;
};

/** Runs the message loop until `predicate` holds or the timeout expires.
 *
 *  The completion callbacks arrive through an AsyncUpdater, so nothing is
 *  delivered unless the message queue is pumped — which in a console test binary
 *  has to be done by hand. */
template <typename Predicate>
bool pumpUntil (Predicate predicate, int timeoutMs = 5000)
{
    const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;

    while (! predicate())
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;

        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    }

    return true;
}

std::unique_ptr<SessionWriter> aWriter (int elements = 64)
{
    auto writer = std::make_unique<SessionWriter>();
    writer->manifest().setProperty ("plugin", "Triggered Average");

    std::vector<float> values (static_cast<std::size_t> (elements));
    std::iota (values.begin(), values.end(), 1.0f);

    const std::vector<std::int64_t> shape { elements };
    writer->addArray ("sums", std::span (values), std::span (shape));

    return writer;
}

} // namespace

TEST (SessionIoThread, SavesOnABackgroundThreadAndReportsBack)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    juce::MessageManager::getInstance();

    SessionIoThread io;

    std::atomic<bool> finished { false };
    SessionIoThread::SaveResult received;
    std::thread::id callbackThread;

    io.onSaveFinished = [&] (SessionIoThread::SaveResult result)
    {
        callbackThread = std::this_thread::get_id();
        received = std::move (result);
        finished = true;
    };

    const auto callerThread = std::this_thread::get_id();

    ASSERT_TRUE (io.save (aWriter(), target));
    ASSERT_TRUE (pumpUntil ([&] { return finished.load(); }));

    EXPECT_TRUE (received.result.wasOk()) << received.result.getErrorMessage();
    EXPECT_EQ (received.directory, target);
    EXPECT_GT (received.bytesWritten, 0);
    EXPECT_TRUE (target.getChildFile ("manifest.json").existsAsFile());

    // The completion is marshalled back, so the callback runs where the caller
    // is, not on the worker.
    EXPECT_EQ (callbackThread, callerThread);
}

TEST (SessionIoThread, LoadsAndPreloadsTheArrays)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    juce::MessageManager::getInstance();

    ASSERT_TRUE (aWriter (32)->flushToDirectory (target).wasOk());

    SessionIoThread io;

    std::atomic<bool> finished { false };
    SessionIoThread::LoadResult received;

    io.onLoadFinished = [&] (SessionIoThread::LoadResult result)
    {
        received = std::move (result);
        finished = true;
    };

    ASSERT_TRUE (io.load (target));
    ASSERT_TRUE (pumpUntil ([&] { return finished.load(); }));

    ASSERT_TRUE (received.wasOk()) << received.error;
    ASSERT_NE (received.reader, nullptr);

    const auto values = received.reader->readFloat32 ("sums");
    ASSERT_TRUE (values.has_value());
    ASSERT_EQ (values->size(), 32u);
    EXPECT_FLOAT_EQ ((*values)[0], 1.0f);
    EXPECT_FLOAT_EQ ((*values)[31], 32.0f);
}

/** The arrays must survive the directory going away, because the whole point of
 *  preloading is that the message thread does no file I/O when it applies them. */
TEST (SessionIoThread, PreloadedArraysDoNotNeedTheFilesAnyMore)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    juce::MessageManager::getInstance();

    ASSERT_TRUE (aWriter (16)->flushToDirectory (target).wasOk());

    SessionIoThread io;

    std::atomic<bool> finished { false };
    SessionIoThread::LoadResult received;

    io.onLoadFinished = [&] (SessionIoThread::LoadResult result)
    {
        received = std::move (result);
        finished = true;
    };

    ASSERT_TRUE (io.load (target));
    ASSERT_TRUE (pumpUntil ([&] { return finished.load(); }));
    ASSERT_TRUE (received.wasOk()) << received.error;

    target.getChildFile ("arrays").deleteRecursively();

    const auto values = received.reader->readFloat32 ("sums");
    ASSERT_TRUE (values.has_value()) << "the array should have been read into memory already";
    EXPECT_EQ (values->size(), 16u);
}

TEST (SessionIoThread, ReportsAFailedLoadRatherThanStayingSilent)
{
    ScratchDirectory scratch;

    juce::MessageManager::getInstance();

    SessionIoThread io;

    std::atomic<bool> finished { false };
    SessionIoThread::LoadResult received;

    io.onLoadFinished = [&] (SessionIoThread::LoadResult result)
    {
        received = std::move (result);
        finished = true;
    };

    ASSERT_TRUE (io.load (scratch.child ("no_such_session")));
    ASSERT_TRUE (pumpUntil ([&] { return finished.load(); }));

    EXPECT_FALSE (received.wasOk());
    EXPECT_TRUE (received.error.isNotEmpty());
}

/** Pressing Save twice means "save", not "save twice" — and a queued second job
 *  would overwrite the first one's result. */
TEST (SessionIoThread, RefusesASecondJobWhileOneIsRunning)
{
    ScratchDirectory scratch;

    juce::MessageManager::getInstance();

    SessionIoThread io;

    std::atomic<int> completions { 0 };
    io.onSaveFinished = [&] (SessionIoThread::SaveResult) { ++completions; };

    // Big enough that the second request lands while the first is still in the
    // worker, on any disk worth testing on.
    ASSERT_TRUE (io.save (aWriter (1 << 20), scratch.child ("first")));

    const bool secondRefused = ! io.save (aWriter(), scratch.child ("second"))
                               || ! io.load (scratch.child ("third"));
    EXPECT_TRUE (secondRefused);

    ASSERT_TRUE (pumpUntil ([&] { return completions.load() >= 1; }));

    EXPECT_TRUE (scratch.child ("first").getChildFile ("manifest.json").existsAsFile());
}

/** After a job completes the thread must accept the next one, including one
 *  queued from inside the completion callback. */
TEST (SessionIoThread, AcceptsAnotherJobAfterOneFinishes)
{
    ScratchDirectory scratch;

    juce::MessageManager::getInstance();

    SessionIoThread io;

    std::atomic<int> completions { 0 };
    std::atomic<bool> secondAccepted { false };

    io.onSaveFinished = [&] (SessionIoThread::SaveResult)
    {
        if (++completions == 1)
            secondAccepted = io.save (aWriter(), scratch.child ("second"));
    };

    ASSERT_TRUE (io.save (aWriter(), scratch.child ("first")));
    ASSERT_TRUE (pumpUntil ([&] { return completions.load() >= 2; }));

    EXPECT_TRUE (secondAccepted);
    EXPECT_TRUE (scratch.child ("first").getChildFile ("manifest.json").existsAsFile());
    EXPECT_TRUE (scratch.child ("second").getChildFile ("manifest.json").existsAsFile());
}

/** Destroying the thread with a job in flight must not crash or leave a staging
 *  directory where the session should be. */
TEST (SessionIoThread, ShutsDownCleanlyWithAJobInFlight)
{
    ScratchDirectory scratch;
    const auto target = scratch.child ("session");

    juce::MessageManager::getInstance();

    {
        SessionIoThread io;
        io.save (aWriter (1 << 18), target);
    }

    EXPECT_FALSE (scratch.child (".session.partial").exists());
}
