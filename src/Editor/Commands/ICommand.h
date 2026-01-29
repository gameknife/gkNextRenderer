#pragma once

#include "Common/CoreMinimal.hpp"
#include <string>
#include <memory>

namespace Editor
{
    /**
     * Base interface for all editor commands supporting undo/redo.
     * Commands encapsulate a single atomic change that can be executed and reverted.
     */
    class ICommand
    {
    public:
        virtual ~ICommand() = default;
        
        /**
         * Execute the command (do/redo)
         * @return true if execution was successful
         */
        virtual bool Execute() = 0;
        
        /**
         * Undo the command, reverting to the state before execution
         * @return true if undo was successful
         */
        virtual bool Undo() = 0;
        
        /**
         * Get a human-readable description of this command for UI display
         */
        virtual std::string GetDescription() const = 0;
        
        /**
         * Check if this command can be merged with another command of the same type.
         * Useful for continuous property changes (e.g., dragging a slider).
         */
        virtual bool CanMergeWith(const ICommand* other) const { return false; }
        
        /**
         * Merge another command into this one.
         * Only called if CanMergeWith returns true.
         */
        virtual void MergeWith(const ICommand* other) {}
    };
    
    using CommandPtr = std::unique_ptr<ICommand>;
}
