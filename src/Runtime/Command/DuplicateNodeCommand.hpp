#pragma once

#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Command/ICommand.hpp"

#include <memory>
#include <string>

namespace Assets
{
    class Scene;
    class Node;
}

class DuplicateNodeCommand final : public ICommand
{
public:
    DuplicateNodeCommand(Assets::Scene& scene, uint32_t sourceInstanceId);

    bool Execute() override;
    bool Undo() override;
    std::string GetDescription() const override { return "Duplicate Node"; }

    uint32_t GetNewInstanceId() const { return newInstanceId_; }

private:
    Assets::Scene* scene_ = nullptr;
    uint32_t sourceInstanceId_ = 0;
    uint32_t newInstanceId_ = 0;
    uint32_t previousSelectedId_ = static_cast<uint32_t>(-1);
    std::shared_ptr<Assets::Node> parent_{};
    std::shared_ptr<Assets::Node> newNode_{};
};
