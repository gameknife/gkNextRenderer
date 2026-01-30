#pragma once

#pragma once

#include "Common/CoreMinimal.hpp"
#include "Runtime/Command/ICommand.hpp"
#include "Assets/Scene.hpp"

#include <memory>
#include <string>
#include <vector>

class DeleteNodeCommand final : public ICommand
{
public:
    DeleteNodeCommand(Assets::Scene& scene, uint32_t instanceId);

    bool Execute() override;
    bool Undo() override;
    std::string GetDescription() const override { return "Delete Node"; }

private:
    Assets::Scene* scene_ = nullptr;
    uint32_t instanceId_ = 0;
    uint32_t previousSelectedId_ = static_cast<uint32_t>(-1);
    std::shared_ptr<Assets::Node> parent_{};
    std::shared_ptr<Assets::Node> root_{};
    std::vector<Assets::Scene::RemovedNodeEntry> removedEntries_{};
};
