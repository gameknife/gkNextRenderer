#pragma once

#include "Engine/Runtime/Command/ICommand.hpp"

#include "Engine/Assets/Core/Scene.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Assets
{
    class Node;
}

class DeleteNodesCommand final : public ICommand
{
public:
    DeleteNodesCommand(Assets::Scene& scene, std::vector<uint32_t> instanceIds);

    bool Execute() override;
    bool Undo() override;
    std::string GetDescription() const override;

private:
    void InitializeRootSelection();
    void RestoreSelection() const;

    struct FRemovedHierarchy
    {
        uint32_t rootId = static_cast<uint32_t>(-1);
        std::shared_ptr<Assets::Node> root{};
        std::shared_ptr<Assets::Node> parent{};
        std::vector<Assets::Scene::RemovedNodeEntry> removedEntries{};
    };

    Assets::Scene* scene_ = nullptr;
    std::vector<uint32_t> instanceIds_;
    std::vector<uint32_t> rootInstanceIds_;
    std::vector<FRemovedHierarchy> removedHierarchies_;
    std::vector<uint32_t> previousSelection_;
    uint32_t previousSelectedId_ = static_cast<uint32_t>(-1);
    bool initialized_ = false;
};
