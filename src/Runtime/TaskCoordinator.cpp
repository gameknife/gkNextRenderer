#include "TaskCoordinator.hpp"

#include <chrono>

TaskThread::TaskThread(TaskCoordinator* coordinator)
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

            ResTask task;
            if (taskQueue_.dequeue(task, false))
            {
                task.task_func(task);

                // sync add to mainthread complete queue
                TaskCoordinator::GetInstance()->MarkTaskComplete(task);
            }
            else
            {
                complete_->set();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }));
}

void TaskCoordinator::TestCase()
{
    TaskCoordinator taskCoordinator;
}

uint32_t TaskCoordinator::AddTask( ResTask::TaskFunc task_func, ResTask::TaskFunc complete_func, uint8_t priority)
{
    static uint32_t task_id = 0;
    ResTask task;
    task.task_id = task_id++;
    task.priority = priority;
    task.task_func = std::move(task_func);
    task.complete_func = std::move(complete_func);
#if __APPLE__
    mainthreadTaskQueue_.enqueue(task);
    return task.task_id;
#endif
    threads_[priority]->taskQueue_.enqueue(task);
    return task.task_id;
}

uint32_t TaskCoordinator::AddParralledTask(ResTask::TaskFunc task_func, ResTask::TaskFunc complete_func)
{
    static uint32_t task_id = 0;
    ResTask task;
    task.task_id = task_id++;
    task.priority = 3;
    task.task_func = std::move(task_func);
    task.complete_func = std::move(complete_func);

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


uint32_t TaskCoordinator::GetMainTaskCount()
{
    uint32_t count = 0;
    for ( auto& thread : threads_ )
    {
        count += uint32_t(thread->taskQueue_.size());
    }
    return count;
}

bool TaskCoordinator::IsAllTaskComplete(std::vector<uint32_t>& tasks)
{
    for (uint32_t task_id : tasks)
    {
        if ( !completedTaskIds_.contains(task_id) )
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
        auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(currentTime - startTime);
        if (elapsedTime.count() > 2000) // 2ms = 2000µs
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
                thread->complete_->reset();
                thread->taskQueue_.enqueue(task);
            }
        }
    }
}

std::unique_ptr<TaskCoordinator> TaskCoordinator::instance_;