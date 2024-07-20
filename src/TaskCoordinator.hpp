#pragma once
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <iostream>

namespace details
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

struct event_signal final
{
    event_signal() noexcept : m_signaled{ false } {}

    bool is_set() const
    {
        std::lock_guard<std::mutex> mutexGuard{ m_mutex };
        return m_signaled.load();
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
    details::atomic_acq_rel<bool> m_signaled;
};

typedef std::function<void ()> TaskFunc;
struct ResTask
{
    uint32_t task_id;
    uint8_t priority;
    TaskFunc task_func;
};

class TaskThread
{
public:
    TaskThread()
    {
        complete_.reset(new event_signal());
        terminate_.reset(new event_signal());
        thread_.reset(new std::thread([this] {
            while (true)
            {
                if(terminate_->is_set())
                {
                    break;
                }
                
                if (!taskQueue_.empty())
                {
                    auto task = taskQueue_.front();
                    taskQueue_.pop();
                    task.task_func();
                }
                else
                {
                    complete_->set();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }));
    }
    ~TaskThread()
    {
        std::cout<< "TaskThread " << thread_->get_id() << " request shutting down, " << "wait for task complete." << "remain: " << taskQueue_.size() <<std::endl;
        complete_->wait();
        terminate_->set();
        thread_->join();
    }

    std::unique_ptr<event_signal> terminate_;
    std::unique_ptr<event_signal> complete_;
    std::unique_ptr<std::thread> thread_;
    std::queue<ResTask> taskQueue_;
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
    }

    ~TaskCoordinator()
    {
        std::cout<< "TaskCoordinator request shutting down, wait for TaskThread. " << "remain: " << threads_.size() <<std::endl;
        for (auto& thread : threads_)
        {
            thread.reset();
        }
        std::cout<< "TaskCoordinator shut down." <<std::endl;
    }
    
    uint32_t AddTask( TaskFunc task_func, uint8_t priority = 0) const
    {
        static uint32_t task_id = 0;
        ResTask task;
        task.task_id = task_id++;
        task.priority = priority;
        task.task_func = std::move(task_func);
        threads_[priority]->taskQueue_.push(task);
        return task.task_id;
    }

    void WaitForTask(uint32_t task_id)
    {
        // wait for specific task to complete, like sync load.
        // if task_id not found, it has been down, return immediately.
    }

    static void TestCase();
private:
    std::vector< std::unique_ptr<TaskThread> > threads_;
};

