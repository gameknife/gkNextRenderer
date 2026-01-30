#include "Runtime/Command/CommandHistory.hpp"
#include "Runtime/Command/CommandHistory.hpp"

#include <spdlog/spdlog.h>

namespace
{
    class GroupCommand final : public ICommand
    {
    public:
        GroupCommand(std::vector<CommandPtr>&& commands, std::string description)
            : commands_(std::move(commands))
            , description_(std::move(description))
        {
        }

        bool Execute() override
        {
            for (auto& cmd : commands_)
            {
                if (!cmd->Execute())
                {
                    return false;
                }
            }
            return true;
        }

        bool Undo() override
        {
            for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
            {
                if (!(*it)->Undo())
                {
                    return false;
                }
            }
            return true;
        }

        std::string GetDescription() const override
        {
            return description_;
        }

    private:
        std::vector<CommandPtr> commands_;
        std::string description_;
    };
}

bool CommandHistory::Execute(CommandPtr command)
{
    if (!command)
    {
        return false;
    }

    if (inGroup_)
    {
        command->Execute();
        groupCommands_.push_back(std::move(command));
        NotifyHistoryChanged();
        return true;
    }

    if (mergeEnabled_ && !undoStack_.empty())
    {
        ICommand* prevCommand = undoStack_.back().get();
        if (prevCommand->CanMergeWith(command.get()))
        {
            prevCommand->MergeWith(command.get());
            command->Execute();
            NotifyHistoryChanged();
            return true;
        }
    }

    if (command->Execute())
    {
        SPDLOG_DEBUG("Executed command: {}", command->GetDescription());

        redoStack_.clear();
        undoStack_.push_back(std::move(command));

        while (undoStack_.size() > maxHistorySize)
        {
            undoStack_.pop_front();
        }

        NotifyHistoryChanged();
        return true;
    }

    SPDLOG_WARN("Command execution failed: {}", command->GetDescription());
    return false;
}

bool CommandHistory::Undo()
{
    if (undoStack_.empty())
    {
        return false;
    }

    auto command = std::move(undoStack_.back());
    undoStack_.pop_back();

    if (command->Undo())
    {
        SPDLOG_DEBUG("Undone command: {}", command->GetDescription());
        redoStack_.push_back(std::move(command));
        NotifyHistoryChanged();
        return true;
    }

    SPDLOG_WARN("Undo failed: {}", command->GetDescription());
    undoStack_.push_back(std::move(command));
    return false;
}

bool CommandHistory::Redo()
{
    if (redoStack_.empty())
    {
        return false;
    }

    auto command = std::move(redoStack_.back());
    redoStack_.pop_back();

    if (command->Execute())
    {
        SPDLOG_DEBUG("Redone command: {}", command->GetDescription());
        undoStack_.push_back(std::move(command));
        NotifyHistoryChanged();
        return true;
    }

    SPDLOG_WARN("Redo failed: {}", command->GetDescription());
    redoStack_.push_back(std::move(command));
    return false;
}

std::string CommandHistory::GetUndoDescription() const
{
    if (undoStack_.empty())
    {
        return "";
    }
    return undoStack_.back()->GetDescription();
}

std::string CommandHistory::GetRedoDescription() const
{
    if (redoStack_.empty())
    {
        return "";
    }
    return redoStack_.back()->GetDescription();
}

std::vector<std::string> CommandHistory::GetUndoStackDescriptions() const
{
    std::vector<std::string> descriptions;
    descriptions.reserve(undoStack_.size());
    for (auto it = undoStack_.rbegin(); it != undoStack_.rend(); ++it)
    {
        descriptions.push_back((*it)->GetDescription());
    }
    return descriptions;
}

std::vector<std::string> CommandHistory::GetRedoStackDescriptions() const
{
    std::vector<std::string> descriptions;
    descriptions.reserve(redoStack_.size());
    for (auto it = redoStack_.rbegin(); it != redoStack_.rend(); ++it)
    {
        descriptions.push_back((*it)->GetDescription());
    }
    return descriptions;
}

void CommandHistory::Clear()
{
    undoStack_.clear();
    redoStack_.clear();
    groupCommands_.clear();
    inGroup_ = false;
    NotifyHistoryChanged();
}

void CommandHistory::BeginGroup(const std::string& description)
{
    if (inGroup_)
    {
        SPDLOG_WARN("Already in a command group, ending previous group");
        EndGroup();
    }
    inGroup_ = true;
    groupDescription_ = description;
    groupCommands_.clear();
}

void CommandHistory::EndGroup()
{
    if (!inGroup_)
    {
        return;
    }

    inGroup_ = false;

    if (groupCommands_.empty())
    {
        return;
    }

    auto groupCmd = std::make_unique<GroupCommand>(
        std::move(groupCommands_),
        groupDescription_);

    redoStack_.clear();
    undoStack_.push_back(std::move(groupCmd));

    while (undoStack_.size() > maxHistorySize)
    {
        undoStack_.pop_front();
    }

    NotifyHistoryChanged();
}

void CommandHistory::NotifyHistoryChanged()
{
    if (onHistoryChanged_)
    {
        onHistoryChanged_();
    }
}
