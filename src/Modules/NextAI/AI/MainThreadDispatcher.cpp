#include "Modules/NextAI/AI/MainThreadDispatcher.hpp"

#include <stdexcept>

namespace NextAI
{
    std::future<std::string> FMainThreadDispatcher::Submit(FTask task)
    {
        auto promise = std::make_shared<std::promise<std::string>>();
        std::future<std::string> future = promise->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back({std::move(task), promise});
        }
        return future;
    }

    std::string FMainThreadDispatcher::SubmitAndWait(FTask task, std::chrono::milliseconds timeout)
    {
        std::future<std::string> future = Submit(std::move(task));
        if (future.wait_for(timeout) != std::future_status::ready)
        {
            throw std::runtime_error("FMainThreadDispatcher: task timed out");
        }
        return future.get();
    }

    size_t FMainThreadDispatcher::DrainOnce()
    {
        std::vector<FQueuedTask> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            batch.swap(queue_);
        }
        for (auto& entry : batch)
        {
            try
            {
                std::string result = entry.task();
                entry.promise->set_value(std::move(result));
            }
            catch (...)
            {
                try
                {
                    entry.promise->set_exception(std::current_exception());
                }
                catch (...)
                {
                    // promise already satisfied; ignore.
                }
            }
        }
        return batch.size();
    }

    size_t FMainThreadDispatcher::QueuedCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
}
