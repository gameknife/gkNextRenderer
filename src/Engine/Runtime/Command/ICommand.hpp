#pragma once

#pragma once

#include "Engine/Common/CoreMinimal.hpp"


namespace Runtime::Command
{

class ICommand
{
public:
    virtual ~ICommand() = default;

    virtual bool Execute() = 0;
    virtual bool Undo() = 0;
    virtual std::string GetDescription() const = 0;

    virtual bool CanMergeWith(const ICommand* other) const { return false; }
    virtual void MergeWith(const ICommand* other) {}
};

using CommandPtr = std::unique_ptr<ICommand>;

}
