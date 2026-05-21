#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Command/ICommand.hpp"

#include <string>

namespace Runtime::Command
{

class RenameNodeCommand final : public ICommand
{
public:
    RenameNodeCommand(Assets::Scene& scene, uint32_t instanceId, std::string newName);
    bool Execute() override;
    bool Undo() override;
    std::string GetDescription() const override { return "Rename Node"; }

private:
    Assets::Scene* scene_ = nullptr;
    uint32_t instanceId_ = 0;
    std::string oldName_{};
    std::string newName_{};
};

}
