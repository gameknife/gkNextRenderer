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