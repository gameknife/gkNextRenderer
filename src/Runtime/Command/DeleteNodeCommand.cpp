#include "Runtime/Command/DeleteNodeCommand.hpp"

#include "Assets/Node.h"
#include "Assets/Scene.hpp"

DeleteNodeCommand::DeleteNodeCommand(Assets::Scene& scene, uint32_t instanceId)
    : scene_(&scene)
    , instanceId_(instanceId)
{
}

bool DeleteNodeCommand::Execute()
{
    if (!scene_)
    {
        return false;
    }

    root_ = scene_->GetNodeSharedByInstanceId(instanceId_);
    if (!root_)
    {
        return false;
    }

    previousSelectedId_ = scene_->GetSelectedId();
    removedEntries_ = scene_->RemoveNodeHierarchy(instanceId_, parent_);
    if (removedEntries_.empty())
    {
        return false;
    }

    scene_->SetSelectedId(static_cast<uint32_t>(-1));
    scene_->MarkDirty();
    scene_->GetCPUAccelerationStructure().UpdateBVH(*scene_);
    return true;
}

void DeleteNodeCommand::Undo()
{
    if (!scene_ || removedEntries_.empty())
    {
        return;
    }

    scene_->RestoreNodes(removedEntries_, parent_, root_);
    scene_->SetSelectedId(previousSelectedId_);
    scene_->MarkDirty();
    scene_->GetCPUAccelerationStructure().UpdateBVH(*scene_);
}

void DeleteNodeCommand::Redo()
{
    if (!scene_ || !root_)
    {
        return;
    }

    removedEntries_.clear();
    removedEntries_ = scene_->RemoveNodeHierarchy(instanceId_, parent_);
    scene_->SetSelectedId(static_cast<uint32_t>(-1));
    scene_->MarkDirty();
    scene_->GetCPUAccelerationStructure().UpdateBVH(*scene_);
}
