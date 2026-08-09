#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Modules/DevTools/Command/SelectionCommandUtils.hpp"

#include <algorithm>

namespace Runtime::Command
{

DeleteNodesCommand::DeleteNodesCommand(Assets::Scene& scene, std::vector<uint32_t> instanceIds)
    : scene_(&scene)
    , instanceIds_(std::move(instanceIds))
{
}

bool DeleteNodesCommand::Execute()
{
    if (scene_ == nullptr)
    {
        return false;
    }

    if (!initialized_)
    {
        previousSelection_ = scene_->GetSelectedIds();
        previousSelectedId_ = scene_->GetSelectedId();
        InitializeRootSelection();
        initialized_ = true;
    }

    if (rootInstanceIds_.empty())
    {
        return false;
    }

    removedHierarchies_.clear();
    removedHierarchies_.reserve(rootInstanceIds_.size());

    for (uint32_t rootId : rootInstanceIds_)
    {
        auto root = scene_->GetNodeSharedByInstanceId(rootId);
        if (!root)
        {
            continue;
        }

        std::shared_ptr<Assets::Node> parent;
        std::vector<Assets::Scene::RemovedNodeEntry> removedEntries =
            scene_->RemoveNodeHierarchy(rootId, parent);
        if (removedEntries.empty())
        {
            continue;
        }

        removedHierarchies_.push_back(FRemovedHierarchy{rootId, root, parent, std::move(removedEntries)});
    }

    if (removedHierarchies_.empty())
    {
        return false;
    }

    scene_->ClearSelection();
    scene_->MarkDirty();
    return true;
}

bool DeleteNodesCommand::Undo()
{
    if (scene_ == nullptr || removedHierarchies_.empty())
    {
        return false;
    }

    for (auto it = removedHierarchies_.rbegin(); it != removedHierarchies_.rend(); ++it)
    {
        scene_->RestoreNodes(it->removedEntries, it->parent, it->root);
    }

    RestoreSelection();
    scene_->MarkDirty();
    return true;
}

std::string DeleteNodesCommand::GetDescription() const
{
    const size_t count = initialized_ ? rootInstanceIds_.size() : instanceIds_.size();
    return count > 1 ? "Delete Nodes" : "Delete Node";
}

void DeleteNodesCommand::InitializeRootSelection()
{
    if (scene_ == nullptr)
    {
        return;
    }

    rootInstanceIds_ = Runtime::Command::SelectionUtils::BuildRootSelection(*scene_, instanceIds_);
}

void DeleteNodesCommand::RestoreSelection() const
{
    if (scene_ == nullptr)
    {
        return;
    }
    Runtime::Command::SelectionUtils::RestoreSelection(*scene_, previousSelection_, previousSelectedId_);
}

}
