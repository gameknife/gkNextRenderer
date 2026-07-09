#include "Modules/DevTools/Command/RenameNodeCommand.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"

namespace Runtime::Command
{

RenameNodeCommand::RenameNodeCommand(Assets::Scene& scene, uint32_t instanceId, std::string newName)
    : scene_(&scene)
    , instanceId_(instanceId)
    , newName_(std::move(newName))
{
    if (scene_ != nullptr)
    {
        if (Assets::Node* node = scene_->GetNodeByInstanceId(instanceId_))
        {
            oldName_ = node->GetName();
        }
    }
}

bool RenameNodeCommand::Execute()
{
    if (scene_ == nullptr)
    {
        return false;
    }

    Assets::Node* node = scene_->GetNodeByInstanceId(instanceId_);
    if (node == nullptr)
    {
        return false;
    }

    node->SetName(newName_);
    scene_->MarkDirty();
    return true;
}

bool RenameNodeCommand::Undo()
{
    if (scene_ == nullptr)
    {
        return false;
    }

    Assets::Node* node = scene_->GetNodeByInstanceId(instanceId_);
    if (node == nullptr)
    {
        return false;
    }

    node->SetName(oldName_);
    scene_->MarkDirty();
    return true;
}

}
