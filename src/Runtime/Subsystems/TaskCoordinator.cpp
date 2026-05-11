#include "Runtime/Subsystems/TaskCoordinator.hpp"

#include <chrono>

TaskThread::TaskThread(TaskCoordinator* coordinator)
{
    complete_.reset(new event_signal());
    terminate_.reset(new event_signal());
    complete_->set();
    thread_.reset(new std::thread([this] {
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
