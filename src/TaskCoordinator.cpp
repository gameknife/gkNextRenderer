#include "TaskCoordinator.hpp"

#include <iostream>

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
    // taskCoordinator.AddTask([]() { std::this_thread::sleep_for(std::chrono::seconds(1));std::cout << "Thread " << std::this_thread::get_id() << " - Task 1" << std::endl; }, 0);
    // taskCoordinator.AddTask([]() { std::this_thread::sleep_for(std::chrono::seconds(2));std::cout << "Thread " << std::this_thread::get_id() << " - Task 2" << std::endl; }, 1);
    // taskCoordinator.AddTask([]() { std::this_thread::sleep_for(std::chrono::seconds(3));std::cout << "Thread " << std::this_thread::get_id() << " - Task 3" << std::endl; }, 1);
    // taskCoordinator.AddTask([]() { std::this_thread::sleep_for(std::chrono::seconds(4));std::cout << "Thread " << std::this_thread::get_id() << " - Task 4" << std::endl; }, 2);
    // taskCoordinator.AddTask([]() { std::this_thread::sleep_for(std::chrono::seconds(5));std::cout << "Thread " << std::this_thread::get_id() << " - Task 5" << std::endl; }, 2);
    // taskCoordinator.AddTask([]() { std::this_thread::sleep_for(std::chrono::seconds(6));std::cout << "Thread " << std::this_thread::get_id() << " - Task 6" << std::endl; }, 2);
}

std::unique_ptr<TaskCoordinator> TaskCoordinator::instance_;