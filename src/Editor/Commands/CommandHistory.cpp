#include "CommandHistory.h"
#include <spdlog/spdlog.h>

namespace Editor
{
    /**
     * GroupCommand combines multiple commands into a single undoable unit.
     */
    class GroupCommand : public ICommand
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
            // Undo in reverse order
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

    void CommandHistory::Execute(CommandPtr command)
    {
        if (!command)
        {
            return;
        }
        
        // If in a command group, add to group instead of executing directly
        if (inGroup_)
        {
            command->Execute();
            groupCommands_.push_back(std::move(command));
            NotifyHistoryChanged();
            return;
        }
        
        // Try to merge with previous command if enabled
        if (mergeEnabled_ && !undoStack_.empty())
        {
            ICommand* prevCommand = undoStack_.back().get();
            if (prevCommand->CanMergeWith(command.get()))
            {
                prevCommand->MergeWith(command.get());
                command->Execute();  // Execute the new command to update state
                NotifyHistoryChanged();
                return;
            }
        }
        
        // Execute the command
        if (command->Execute())
        {
            SPDLOG_DEBUG("Executed command: {}", command->GetDescription());
            
            // Clear redo stack when new command is executed
            redoStack_.clear();
            
            // Add to undo stack
            undoStack_.push_back(std::move(command));
            
            // Enforce max history size
            while (undoStack_.size() > maxHistorySize)
            {
                undoStack_.pop_front();
            }
            
            NotifyHistoryChanged();
        }
        else
        {
            SPDLOG_WARN("Command execution failed: {}", command->GetDescription());
        }
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
        else
        {
            SPDLOG_WARN("Undo failed: {}", command->GetDescription());
            // Put command back if undo failed
            undoStack_.push_back(std::move(command));
            return false;
        }
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
        else
        {
            SPDLOG_WARN("Redo failed: {}", command->GetDescription());
            // Put command back if redo failed
            redoStack_.push_back(std::move(command));
            return false;
        }
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
        
        // Create a group command from all accumulated commands
        auto groupCmd = std::make_unique<GroupCommand>(
            std::move(groupCommands_),
            groupDescription_
        );
        
        // Don't re-execute, the commands were already executed
        // Just add to undo stack
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
}
