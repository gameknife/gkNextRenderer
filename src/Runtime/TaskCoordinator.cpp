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

std::unique_ptr<TaskCoordinator> TaskCoordinator::instance_;