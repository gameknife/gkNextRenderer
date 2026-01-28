#pragma once

#include "Common/CoreMinimal.hpp"

#include <memory>
#include <vector>

class ICommand
{
public:
    virtual ~ICommand() = default;
    virtual bool Execute() = 0;
    virtual void Undo() = 0;
    virtual void Redo() = 0;
    virtual const char* Name() const { return "Command"; }
};

class CommandSystem
{
public:
    explicit CommandSystem(size_t historyLimit = 100);

    bool ExecuteCommand(std::unique_ptr<ICommand> command);
    bool Undo();
    bool Redo();
    bool CanUndo() const;
    bool CanRedo() const;
    void Clear();

    size_t HistoryLimit() const { return historyLimit_; }
    void SetHistoryLimit(size_t limit);

private:
    void TrimHistory();

    std::vector<std::unique_ptr<ICommand>> undoStack_;
    std::vector<std::unique_ptr<ICommand>> redoStack_;
    size_t historyLimit_ = 100;
};
