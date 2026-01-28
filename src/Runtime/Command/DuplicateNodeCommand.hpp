#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Command/CommandSystem.hpp"

#include <memory>

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
    void Undo() override;
    void Redo() override;
    const char* Name() const override { return "DuplicateNode"; }

    uint32_t GetNewInstanceId() const { return newInstanceId_; }

private:
    Assets::Scene* scene_ = nullptr;
    uint32_t sourceInstanceId_ = 0;
    uint32_t newInstanceId_ = 0;
    uint32_t previousSelectedId_ = static_cast<uint32_t>(-1);
    std::shared_ptr<Assets::Node> parent_{};
    std::shared_ptr<Assets::Node> newNode_{};
};
