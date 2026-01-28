#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Command/CommandSystem.hpp"
#include "Assets/Scene.hpp"

#include <memory>
#include <vector>

class DeleteNodeCommand final : public ICommand
{
public:
    DeleteNodeCommand(Assets::Scene& scene, uint32_t instanceId);

    bool Execute() override;
    void Undo() override;
    void Redo() override;
    const char* Name() const override { return "DeleteNode"; }

private:
    Assets::Scene* scene_ = nullptr;
    uint32_t instanceId_ = 0;
    uint32_t previousSelectedId_ = static_cast<uint32_t>(-1);
    std::shared_ptr<Assets::Node> parent_{};
    std::shared_ptr<Assets::Node> root_{};
    std::vector<Assets::Scene::RemovedNodeEntry> removedEntries_{};
};
