#pragma once

#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Command/ICommand.hpp"

#include <deque>
#include <functional>
#include <string>
#include <vector>

/**
 * Manages the undo/redo stack for commands.
 * Provides functionality for executing, undoing, and redoing commands.
 */
class CommandHistory
{
public:
    // Maximum number of commands to keep in history
    static constexpr size_t maxHistorySize = 100;

    /**
     * Execute a command and add it to the history.
     * This clears the redo stack.
     */
    bool Execute(CommandPtr command);

    /**
     * Undo the most recent command.
     * @return true if there was a command to undo
     */
    bool Undo();

    /**
     * Redo the most recently undone command.
     * @return true if there was a command to redo
     */
    bool Redo();

    /**
     * Check if there are commands that can be undone.
     */
    bool CanUndo() const { return !undoStack_.empty(); }

    /**
     * Check if there are commands that can be redone.
     */
    bool CanRedo() const { return !redoStack_.empty(); }

    /**
     * Get description of the command that would be undone.
     */
    std::string GetUndoDescription() const;

    /**
     * Get description of the command that would be redone.
     */
    std::string GetRedoDescription() const;

    /**
     * Get undo stack descriptions (most recent first).
     */
    std::vector<std::string> GetUndoStackDescriptions() const;

    /**
     * Get redo stack descriptions (most recent first).
     */
    std::vector<std::string> GetRedoStackDescriptions() const;

    /**
     * Clear all history.
     */
    void Clear();

    /**
     * Get number of commands in undo stack.
     */
    size_t GetUndoCount() const { return undoStack_.size(); }

    /**
     * Get number of commands in redo stack.
     */
    size_t GetRedoCount() const { return redoStack_.size(); }

    /**
     * Set callback to be invoked when history changes (for UI updates).
     */
    void SetOnHistoryChanged(std::function<void()> callback) { onHistoryChanged_ = callback; }

    /**
     * Enable or disable command merging for continuous operations.
     */
    void SetMergeEnabled(bool enabled) { mergeEnabled_ = enabled; }
    bool IsMergeEnabled() const { return mergeEnabled_; }

    /**
     * Begin a group of commands that should be treated as a single undo unit.
     */
    void BeginGroup(const std::string& description);

    /**
     * End the current command group.
     */
    void EndGroup();

    /**
     * Check if currently in a command group.
     */
    bool IsInGroup() const { return inGroup_; }

private:
    void NotifyHistoryChanged();

    std::deque<CommandPtr> undoStack_;
    std::deque<CommandPtr> redoStack_;

    std::function<void()> onHistoryChanged_;
    bool mergeEnabled_ = true;
    bool inGroup_ = false;

    // For grouped commands
    std::vector<CommandPtr> groupCommands_;
    std::string groupDescription_;
};
