#include "Runtime/Command/DuplicateNodeCommand.hpp"

#include "Assets/Node.h"
#include "Assets/Scene.hpp"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/RenderComponent.h"

namespace
{
    std::shared_ptr<Assets::Node> CloneNode(const Assets::Node& source, uint32_t newInstanceId)
    {
        auto clone = Assets::Node::CreateNode(
            source.GetName() + "_copy",
            source.Translation(),
            source.Rotation(),
            source.Scale(),
            newInstanceId);

        if (auto render = source.GetComponent<Runtime::RenderComponent>())
        {
            auto newRender = std::make_shared<Runtime::RenderComponent>();
            newRender->SetModelId(render->GetModelId());
            newRender->SetMaterial(render->Materials());
            newRender->SetVisible(render->GetVisible());
            newRender->SetRayCastVisible(render->GetRayCastVisible());
            newRender->SetSkinIndex(render->GetSkinIndex());
            clone->AddComponent(newRender);
        }

        if (auto phys = source.GetComponent<Runtime::PhysicsComponent>())
        {
            auto newPhys = std::make_shared<Runtime::PhysicsComponent>();
            newPhys->SetMobility(phys->GetMobility());
            newPhys->SetPhysicsOffset(phys->GetPhysicsOffset());
            clone->AddComponent(newPhys);
        }

        return clone;
    }
}

DuplicateNodeCommand::DuplicateNodeCommand(Assets::Scene& scene, uint32_t sourceInstanceId)
    : scene_(&scene)
    , sourceInstanceId_(sourceInstanceId)
{
}

bool DuplicateNodeCommand::Execute()
{
    if (!scene_)
    {
        return false;
    }

    auto sourceNode = scene_->GetNodeSharedByInstanceId(sourceInstanceId_);
    if (!sourceNode)
    {
        return false;
    }

    previousSelectedId_ = scene_->GetSelectedId();

    if (!newNode_)
    {
        newInstanceId_ = scene_->GenerateInstanceId();
        newNode_ = CloneNode(*sourceNode, newInstanceId_);

        if (Assets::Node* parent = sourceNode->GetParent())
        {
            parent_ = scene_->GetNodeSharedByInstanceId(parent->GetInstanceId());
        }
    }

    if (parent_)
    {
        newNode_->SetParent(parent_);
    }

    scene_->AddNode(newNode_);
    scene_->SetSelectedId(newInstanceId_);
    scene_->MarkDirty();
    scene_->GetCPUAccelerationStructure().UpdateBVH(*scene_);
    return true;
}

bool DuplicateNodeCommand::Undo()
{
    if (!scene_ || !newNode_)
    {
        return false;
    }

    scene_->RemoveNodeByInstanceId(newInstanceId_);
    scene_->SetSelectedId(previousSelectedId_);
    scene_->MarkDirty();
    scene_->GetCPUAccelerationStructure().UpdateBVH(*scene_);
    return true;
}
