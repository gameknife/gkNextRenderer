#include "Runtime/Command/CommandSystem.hpp"

CommandSystem::CommandSystem(size_t historyLimit)
    : historyLimit_(historyLimit)
{
}

bool CommandSystem::ExecuteCommand(std::unique_ptr<ICommand> command)
{
    if (!command)
    {
        return false;
    }

    if (!command->Execute())
    {
        return false;
    }

    redoStack_.clear();
    undoStack_.push_back(std::move(command));
    TrimHistory();
    return true;
}

bool CommandSystem::Undo()
{
    if (undoStack_.empty())
    {
        return false;
    }

    auto command = std::move(undoStack_.back());
    undoStack_.pop_back();
    command->Undo();
    redoStack_.push_back(std::move(command));
    return true;
}

bool CommandSystem::Redo()
{
    if (redoStack_.empty())
    {
        return false;
    }

    auto command = std::move(redoStack_.back());
    redoStack_.pop_back();
    command->Redo();
    undoStack_.push_back(std::move(command));
    TrimHistory();
    return true;
}

bool CommandSystem::CanUndo() const
{
    return !undoStack_.empty();
}

bool CommandSystem::CanRedo() const
{
    return !redoStack_.empty();
}

void CommandSystem::Clear()
{
    undoStack_.clear();
    redoStack_.clear();
}

void CommandSystem::SetHistoryLimit(size_t limit)
{
    historyLimit_ = limit;
    TrimHistory();
}

void CommandSystem::TrimHistory()
{
    if (historyLimit_ == 0)
    {
        Clear();
        return;
    }

    while (undoStack_.size() > historyLimit_)
    {
        undoStack_.erase(undoStack_.begin());
    }
}
