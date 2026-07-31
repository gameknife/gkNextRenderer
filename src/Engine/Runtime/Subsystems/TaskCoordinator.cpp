#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"

#include <chrono>
#include <SDL3/SDL_thread.h>

namespace Tasks
{

TaskThread::TaskThread(std::string threadName, bool highPriority)
    : threadName_(std::move(threadName)), highPriority_(highPriority)
{
    complete_.reset(new event_signal());
    terminate_.reset(new event_signal());
    complete_->set();
    thread_.reset(new std::thread([this] {
        if (highPriority_ && !SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH))
        {
            spdlog::warn("Failed to set high priority for thread '{}': {}", threadName_, SDL_GetError());
        }
#if WITH_SUPERLUMINAL
        if (!threadName_.empty())
        {
            PerformanceAPI::SetCurrentThreadName(threadName_.c_str());
        }
#endif
        while (true)
        {
            if(terminate_->is_set())
            {
                break;
            }

            ResTask task;
            if (taskQueue_.dequeue(task, false))
            {
                busy_.store(true);
                task.task_func(task);

                // sync add to mainthread complete queue
                TaskCoordinator::GetInstance()->MarkTaskComplete(task);
                busy_.store(false);
                {
                    std::lock_guard<std::mutex> lock(completedTaskMutex_);
                    ++completedTaskCount_;
                }
                completedTaskCondition_.notify_all();
            }
            else
            {
                if (!busy_.load())
                {
                    complete_->set();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }));
}

void TaskCoordinator::TestCase()
{
    TaskCoordinator taskCoordinator;
}

TaskCoordinator::~TaskCoordinator()
{
    for (auto& thread : threads_)
    {
        thread.reset();
    }
    for (auto& thread : lowThreads_)
    {
        thread.reset();
    }
    for (auto& thread : namedThreadPool_)
    {
        thread.reset();
    }
}

uint32_t TaskCoordinator::AddTask( ResTask::TaskFunc taskFunc, ResTask::TaskFunc completeFunc, uint8_t priority)
{
    static uint32_t taskId = 0;
    ResTask task;
    task.task_id = taskId++;
    task.priority = priority;
    task.task_func = std::move(taskFunc);
    task.complete_func = std::move(completeFunc);
    threads_[priority]->EnqueueTask(std::move(task));
    return task.task_id;
}

uint32_t TaskCoordinator::AddMainThreadTask(ResTask::TaskFunc taskFunc, ResTask::TaskFunc completeFunc, uint8_t priority)
{
    static uint32_t taskId = 0;
    ResTask task;
    task.task_id = taskId++;
    task.priority = priority;
    task.task_func = std::move(taskFunc);
    task.complete_func = std::move(completeFunc);
    mainthreadTaskQueue_.enqueue(task);
    return task.task_id;
}

uint32_t TaskCoordinator::AddParralledTask(ResTask::TaskFunc taskFunc, ResTask::TaskFunc completeFunc)
{
    static uint32_t taskId = 0;
    ResTask task;
    task.task_id = taskId++;
    task.priority = 3;
    task.task_func = std::move(taskFunc);
    task.complete_func = std::move(completeFunc);

    parralledTaskQueue_.enqueue(task);

    return task.task_id;
}

uint32_t TaskCoordinator::AddNamedTask(
    ENamedTaskThread namedThread, ResTask::TaskFunc taskFunc, ResTask::TaskFunc completeFunc)
{
    static std::atomic<uint32_t> taskId = 0;
    ResTask task;
    task.task_id = taskId.fetch_add(1, std::memory_order_relaxed);
    task.task_func = std::move(taskFunc);
    task.complete_func = std::move(completeFunc);

    const size_t namedThreadIndex = static_cast<size_t>(namedThread);
    assert(namedThreadIndex < namedThreadPool_.size());
    namedThreadPool_[namedThreadIndex]->EnqueueTask(std::move(task));

    return task.task_id;
}

void TaskCoordinator::WaitForAllParralledTask()
{
    while( parralledTaskQueue_.size() > 0 )
    {
        Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }
    
    // wait for all idle
    while( !IsAllParralledTaskComplete() )
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }
}

void TaskCoordinator::WaitForNamedTask(ENamedTaskThread namedThread)
{
    const size_t namedThreadIndex = static_cast<size_t>(namedThread);
    assert(namedThreadIndex < namedThreadPool_.size());
    namedThreadPool_[namedThreadIndex]->WaitForAllTasks();
}

void TaskCoordinator::WaitForAllTasks()
{
    while (!IsAllTaskComplete())
    {
        Tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}


uint32_t TaskCoordinator::GetMainTaskCount()
{
    uint32_t count = uint32_t(mainthreadTaskQueue_.size());
    for ( auto& thread : threads_ )
    {
        count += uint32_t(thread->taskQueue_.size());
    }
    return count;
}

bool TaskCoordinator::IsAllTaskComplete(std::vector<uint32_t>& tasks)
{
    for (uint32_t taskId : tasks)
    {
        if ( !completedTaskIds_.contains(taskId) )
        {
            return false;
        }
    }
    return true;
}

void TaskCoordinator::Tick()
{
    ResTask task;
    if( mainthreadTaskQueue_.dequeue(task, false))
    {
        task.task_func(task);
        if(task.complete_func != nullptr)
        {
            MarkTaskComplete(task);
        }
    }
    
    auto startTime = std::chrono::high_resolution_clock::now();
    while (completeTaskQueue_.size() > 0)
    {
        // Check if we've exceeded 2ms
        auto currentTime = std::chrono::high_resolution_clock::now();
        auto elapsedTime = std::chrono::duration_cast<std::chrono::milliseconds>(currentTime - startTime);
        if (elapsedTime.count() > 4) // 4ms
        {
            break;
        }

        if (completeTaskQueue_.dequeue(task, false))
        {
            if (task.complete_func != nullptr)
            {
                task.complete_func(task);
            }
            MarkTaskEnd(task);
        }
    }

    // if low threads has idle one, peak a task from parralled queue
    for ( auto& thread : lowThreads_ )
    {
        if( thread->IsIdle() )
        {
            if( parralledTaskQueue_.dequeue(task, false) )
            {
                thread->EnqueueTask(std::move(task));
            }
        }
    }
}

std::unique_ptr<TaskCoordinator> TaskCoordinator::instance_;

TaskCoordinator::TaskCoordinator()
{
    for (int i = 0; i < 4; i++)
    {
        threads_.push_back(std::make_unique<TaskThread>("TaskCoordinator Worker " + std::to_string(i)));
    }

    // Get the number of CPU cores (use half of available cores for low-priority threads)
    unsigned int numCores = std::thread::hardware_concurrency();
    unsigned int lowThreadCount = std::max(1u, numCores / 2);

    // Create low-priority threads based on CPU cores
    for (unsigned int i = 0; i < lowThreadCount; i++)
    {
        lowThreads_.push_back(std::make_unique<TaskThread>("TaskCoordinator Parallel " + std::to_string(i)));
    }

    namedThreadPool_[static_cast<size_t>(ENamedTaskThread::SCENE_UPDATE)] =
        std::make_unique<TaskThread>("TaskCoordinator Scene Update", true);
    namedThreadPool_[static_cast<size_t>(ENamedTaskThread::CPU_AS_BUILD)] =
        std::make_unique<TaskThread>("TaskCoordinator CPU AS Build");
}

bool TaskCoordinator::IsAllParralledTaskComplete()
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

bool TaskCoordinator::IsNamedTaskComplete(ENamedTaskThread namedThread) const
{
    const size_t namedThreadIndex = static_cast<size_t>(namedThread);
    assert(namedThreadIndex < namedThreadPool_.size());
    return namedThreadPool_[namedThreadIndex]->IsIdle();
}

bool TaskCoordinator::IsAllTaskComplete()
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

    for (auto& thread : namedThreadPool_)
    {
        if (!thread->IsIdle() || thread->taskQueue_.size() > 0)
        {
            return false;
        }
    }

    return true;
}

}
