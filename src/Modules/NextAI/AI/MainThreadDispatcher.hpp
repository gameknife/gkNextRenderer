#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <vector>

namespace NextAI
{
    // Marshals tool execution from agent worker threads back onto the main thread.
    // Owner calls DrainOnce() each frame to run pending tasks.
    class FMainThreadDispatcher
    {
    public:
        using FTask = std::function<std::string()>;

        // Submit a task; future resolves with the task's return value once a main-thread
        // DrainOnce() picks it up. Safe to call from any thread.
        std::future<std::string> Submit(FTask task);

        // Submit + wait until the task completes or timeout elapses. The future is
        // discarded internally; throws std::runtime_error on timeout.
        std::string SubmitAndWait(FTask task, std::chrono::milliseconds timeout);

        // Execute all currently queued tasks. Tasks submitted while DrainOnce runs
        // are deferred to the next call to keep the loop bounded.
        size_t DrainOnce();

        size_t QueuedCount() const;

    private:
        struct FQueuedTask
        {
            FTask task;
            std::shared_ptr<std::promise<std::string>> promise;
        };

        mutable std::mutex mutex_;
        std::vector<FQueuedTask> queue_;
    };
}
