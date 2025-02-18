#include "TaskCoordinator.hpp"

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
                if(task.complete_func != nullptr)
                {
                    TaskCoordinator::GetInstance()->MarkTaskComplete(task);
                }
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

uint32_t TaskCoordinator::AddParralledTask(ResTask::TaskFunc task_func)
{
    static uint32_t task_id = 0;
    ResTask task;
    task.task_id = task_id++;
    task.priority = 3;
    task.task_func = std::move(task_func);

    // First try to find an idle thread
    for (auto& thread : lowThreads_)
    {
        if (thread->IsIdle())
        {
            thread->complete_->reset();  // Reset complete signal
            thread->taskQueue_.enqueue(task);
            return task.task_id;
        }
    }

    // If no idle thread found, find thread with least tasks
    size_t minTasks = SIZE_MAX;
    TaskThread* selectedThread = nullptr;
    
    for (auto& thread : lowThreads_)
    {
        size_t queueSize = thread->taskQueue_.size();
        if (queueSize < minTasks)
        {
            minTasks = queueSize;
            selectedThread = thread.get();
        }
    }

    // Assign task to thread with least workload
    selectedThread->complete_->reset();  // Reset complete signal
    selectedThread->taskQueue_.enqueue(task);
    return task.task_id;
}

void TaskCoordinator::WaitForAllParralledTask()
{
    // wait 1ms to start last added tasks
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // wait for all parralled task to complete
    for ( auto& thread : lowThreads_ )
    {
        thread->complete_->wait();
    }
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
    if( completeTaskQueue_.dequeue(task, false) )
    {
        task.complete_func(task);
    }
}

std::unique_ptr<TaskCoordinator> TaskCoordinator::instance_;