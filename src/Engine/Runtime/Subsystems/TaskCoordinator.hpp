#pragma once
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include "Engine/Common/CoreMinimal.hpp"
#include <cstring>
#include <unordered_set>

namespace Tasks::Detail
{
    // Like std::atomic, but defaults to std::memory_order_acq_rel (or memory_order_acquire or memory_order_release,
    // as appropriate) rather than memory_order_seq_cst.
    // Does not include some parts of std::atomic not currently used by callers.
    template <typename T>
    struct atomic_acq_rel final
    {
        constexpr atomic_acq_rel() noexcept = default;

        constexpr atomic_acq_rel(T desired) noexcept : m_value{ std::forward<T>(desired) } {}

        atomic_acq_rel(const atomic_acq_rel&) = delete;
        atomic_acq_rel(atomic_acq_rel&&) = delete;

        ~atomic_acq_rel() noexcept = default;

        atomic_acq_rel& operator=(const atomic_acq_rel&) = delete;
        atomic_acq_rel& operator=(const atomic_acq_rel&) volatile = delete;
        atomic_acq_rel& operator=(atomic_acq_rel&&) = delete;

        T operator=(T desired) noexcept
        {
            store(std::forward<T>(desired));
            return std::forward<T>(desired);
        }

        T operator=(T desired) volatile noexcept
        {
            store(std::forward<T>(desired));
            return std::forward<T>(desired);
        }

        operator T() const noexcept { return std::forward<T>(load()); }

        operator T() const volatile noexcept { return std::forward<T>(load()); }

        void store(T desired) noexcept { m_value.store(std::forward<T>(desired), std::memory_order_release); }

        void store(T desired) volatile noexcept { m_value.store(std::forward<T>(desired, std::memory_order_release)); }

        T load() const noexcept { return std::forward<T>(m_value.load(std::memory_order_acquire)); }

        T load() const volatile noexcept { return std::forward<T>(m_value.load(std::memory_order_acquire)); }

        T exchange(T desired) noexcept
        {
            return std::forward<T>(m_value.exchange(std::forward<T>(desired), std::memory_order_acq_rel));
        }

        T exchange(T desired) volatile noexcept
        {
            return std::forward<T>(m_value.exchange(std::forward<T>(desired), std::memory_order_acq_rel));
        }

        bool compare_exchange_weak(T& expected, T desired) noexcept
        {
            return m_value.compare_exchange_weak(
                std::forward<T&>(expected), std::forward<T>(desired), std::memory_order_acq_rel);
        }

        bool compare_exchange_weak(T& expected, T desired) volatile noexcept
        {
            return m_value.compare_exchange_weak(
                std::forward<T&>(expected), std::forward<T>(desired), std::memory_order_acq_rel);
        }

        bool compare_exchange_strong(T& expected, T desired) noexcept
        {
            return m_value.compare_exchange_strong(
                std::forward<T&>(expected), std::forward<T>(desired), std::memory_order_acq_rel);
        }

        bool compare_exchange_strong(T& expected, T desired) volatile noexcept
        {
            return m_value.compare_exchange_strong(
                std::forward<T&>(expected), std::forward<T>(desired), std::memory_order_acq_rel);
        }

    private:
        std::atomic<T> m_value;
    };
}

namespace Tasks
{

struct event_signal final
{
    event_signal() noexcept : m_signaled{ false } {}

    bool is_set() const
    {
        std::lock_guard<std::mutex> mutexGuard{ m_mutex };
        return m_signaled.load();
    }

    void reset() noexcept
    {
        std::lock_guard<std::mutex> mutexGuard{ m_mutex };
        m_signaled = false;
    }

    void wait() const
    {
        std::unique_lock<std::mutex> mutexLock{ m_mutex };
        m_condition.wait(mutexLock, [this]() noexcept { return m_signaled.load(); });
    }

    template <typename Rep, typename Period>
    bool wait_for(const std::chrono::duration<Rep, Period>& rel_time) const
    {
        std::unique_lock<std::mutex> mutexLock{ m_mutex };
        return m_condition.wait_for(mutexLock, rel_time, [this]() { return m_signaled.load(); });
    }

    template <typename Rep, typename Period>
    void wait_for_or_throw(const std::chrono::duration<Rep, Period>& rel_time) const
    {
        if (!wait_for(rel_time))
        {
            throw std::runtime_error{ "Wait timed out." };
        }
    }

    void set() noexcept
    {
        // std::lock_guard constructor is not technically noexcept, but the only possible exception would be from
        // std::mutex.lock, which itself is not technically noexcept, but the underlying implementation on MSVC
        // (_Mtx_lock) appears never to return failure/throw (ultimately calls AcquireSRWLockExclusive, which never
        // fails).
        // Regardless, the C++20 standard requires that co_await of a final_suspend not be potentially throwing, and
        // this member is designed to be called by a final_suspend for awaitable_get, so treat any exception from
        // other Standard Library implementations here as fatal.
        std::lock_guard<std::mutex> mutexGuard{ m_mutex };
        m_signaled = true;
        m_condition.notify_all();
    }

private:
    mutable std::condition_variable m_condition;
    mutable std::mutex m_mutex;
    Tasks::Detail::atomic_acq_rel<bool> m_signaled;
};

template <class T>
class tsqueue
{
public:
    tsqueue() : q(), m(), c() {}

    ~tsqueue() {}

    // Add an element to the queue.
    void enqueue(T t)
    {
        std::lock_guard<std::mutex> lock(m);
        q.push(t);
        c.notify_one();
    }
    
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(m);
        return q.size();
    }
    
    // Get the front element.
    // If the queue is empty, wait till a element is avaiable.
    bool dequeue(T& result, bool wait)
    {
        std::unique_lock<std::mutex> lock(m);
        if(wait)
        {
            while (q.empty())
            {
                // release lock as long as the wait and reaquire it afterwards.
                c.wait(lock);
            }     
        }
        else
        {
            if( q.empty() )
            {
                return false;
            }
        }

        result = q.front();
        q.pop();
        return true;
    }

private:
    std::queue<T> q;
    mutable std::mutex m;
    std::condition_variable c;
};

struct ResTask
{
    typedef std::function<void (ResTask& task)> TaskFunc;
    
    uint32_t task_id = 0;
    uint8_t priority = 0;
    TaskFunc task_func;
    TaskFunc complete_func;
    uint8_t task_context_1k[1024] = {};

    template<typename T>
    void SetContext(T& context)
    {
        std::memcpy( task_context_1k, &context, std::min(size_t(1024), sizeof(T)) );
    }
    template<typename T>
    void GetContext(T& context)
    {
        std::memcpy( &context, task_context_1k, std::min(size_t(1024), sizeof(T)) );
    }
    
};

class TaskCoordinator;

class TaskThread
{
public:
    TaskThread(TaskCoordinator* coordinator = nullptr);
   
    ~TaskThread()
    {
        complete_->wait();
        terminate_->set();
        thread_->join();
        //thread_->detach();
    }

    bool IsIdle()
    {
        return !busy_.load() && taskQueue_.size() == 0 && complete_->is_set();
    }

    void EnqueueTask(ResTask task)
    {
        complete_->reset();
        taskQueue_.enqueue(std::move(task));
    }

    std::unique_ptr<event_signal> terminate_;
    std::unique_ptr<event_signal> complete_;
    std::unique_ptr<std::thread> thread_;
    tsqueue<ResTask> taskQueue_;
    Tasks::Detail::atomic_acq_rel<bool> busy_{ false };
};

class TaskCoordinator
{
public:
    TaskCoordinator()
    {
        for (int i = 0; i < 4; i++)
        {
            threads_.push_back(std::make_unique<TaskThread>());
        }

        // Get the number of CPU cores (use half of available cores for low-priority threads)
        unsigned int numCores = std::thread::hardware_concurrency();
        unsigned int lowThreadCount = std::max(1u, numCores / 1);

        // Create low-priority threads based on CPU cores
        for (unsigned int i = 0; i < lowThreadCount; i++)
        {
            lowThreads_.push_back(std::make_unique<TaskThread>());
        }

        //SPDLOG_INFO("low parallel thread count: {}", lowThreadCount);
    }

    ~TaskCoordinator();
   

    void MarkTaskComplete(const ResTask& task)
    {
        completeTaskQueue_.enqueue(task);
    }

    void MarkTaskEnd(const ResTask& task)
    {
        completedTaskIds_.insert(task.task_id);
    }
    
    // Queues worker-thread CPU tasks. Use this for long-running work that must not touch scene/UI state directly.
    // complete_func runs back on the coordinator path after task_func finishes.
    uint32_t AddTask( ResTask::TaskFunc task_func, ResTask::TaskFunc complete_func, uint8_t priority = 0);
    uint32_t AddMainThreadTask(ResTask::TaskFunc task_func, ResTask::TaskFunc complete_func, uint8_t priority = 0);
    uint32_t AddParralledTask( ResTask::TaskFunc task_func, ResTask::TaskFunc complete_func );

    void WaitForTask(uint32_t task_id)
    {
        // wait for specific task to complete, like sync load.
        // if task_id not found, it has been down, return immediately.
    }

    void WaitForAllParralledTask();
    void WaitForAllTasks();
    
    bool IsAllParralledTaskComplete()
    {
        for ( auto& thread : lowThreads_ )
        {
            if( !thread->IsIdle() )
            {
                return false;
            }
        }
        return true;
    }

    bool IsAllTaskComplete()
    {
        if (mainthreadTaskQueue_.size() > 0)
        {
            return false;
        }

        for (auto& thread : threads_)
        {
            if (!thread->IsIdle() || thread->taskQueue_.size() > 0)
            {
                return false;
            }
        }

        if (completeTaskQueue_.size() > 0 || parralledTaskQueue_.size() > 0)
        {
            return false;
        }

        for (auto& thread : lowThreads_)
        {
            if (!thread->IsIdle() || thread->taskQueue_.size() > 0)
            {
                return false;
            }
        }

        return true;
    }

    void CancelAllParralledTasks()
    {
        ResTask task;
        while(parralledTaskQueue_.dequeue(task, false))
        {
        }
    }

    uint32_t GetParralledTaskCount()
    {
        return uint32_t(parralledTaskQueue_.size());
    }

    uint32_t GetMainTaskCount();

    uint32_t GetComleteTaskQueueCount()
    {
        return uint32_t(completeTaskQueue_.size());
    }

    bool IsAllTaskComplete(std::vector<uint32_t>& tasks);

    void Tick();

    static TaskCoordinator* GetInstance()
    {
        if(instance_ == nullptr)
        {
            instance_.reset(new TaskCoordinator());
        }
        return instance_.get();
    }
    
    static void DestroyInstance()
    {
        instance_.reset(nullptr);
    }

private:
    std::vector< std::unique_ptr<TaskThread> > threads_;
    // low-level thread, use for parrallel task
    std::vector< std::unique_ptr<TaskThread> > lowThreads_;
    tsqueue<ResTask> mainthreadTaskQueue_;
    tsqueue<ResTask> completeTaskQueue_;
    tsqueue<ResTask> parralledTaskQueue_;

    std::unordered_set<uint32_t> completedTaskIds_;
private:
    static std::unique_ptr<TaskCoordinator> instance_;
    static void TestCase();
};

}
