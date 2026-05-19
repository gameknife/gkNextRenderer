#include "Engine/Runtime/Command/DuplicateNodesCommand.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Command/SelectionCommandUtils.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.h"

#include <algorithm>
#include <unordered_set>

namespace Runtime::Command
{

namespace
{

    std::shared_ptr<Assets::Node> CloneNode(const Assets::Node& source, uint32_t newInstanceId)
    {
        std::shared_ptr<Assets::Node> clone;
        if (auto render = source.GetComponent<Runtime::RenderComponent>())
        {
            clone = Assets::SceneBuilder::CreateRenderNode(source.GetName() + "_copy",
                                                   source.Translation(),
                                                   source.Scale(),
                                                   newInstanceId,
                                                   render->GetModelId(),
                                                   render->GetMaterials(),
                                                   render->GetVisible(),
                                                   source.Rotation(),
                                                   render->GetRayCastVisible());
            if (auto newRender = clone->GetComponent<Runtime::RenderComponent>())
            {
                newRender->SetSkinIndex(render->GetSkinIndex());
            }
        }
        else
        {
            clone = Assets::Node::CreateNode(
                source.GetName() + "_copy",
                source.Translation(),
                source.Rotation(),
                source.Scale(),
                newInstanceId);
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

DuplicateNodesCommand::DuplicateNodesCommand(Assets::Scene& scene, std::vector<uint32_t> sourceIds)
    : scene_(&scene)
    , sourceIds_(std::move(sourceIds))
{
}

bool DuplicateNodesCommand::Execute()
{
    if (scene_ == nullptr)
    {
        return false;
    }

    if (!initialized_)
    {
        previousSelection_ = scene_->GetSelectedIds();
        previousSelectedId_ = scene_->GetSelectedId();
        InitializeSourceSelection();
        initialized_ = true;
    }

    if (uniqueSourceIds_.empty())
    {
        return false;
    }

    newInstanceIds_.clear();
    if (duplicateEntries_.empty())
    {
        duplicateEntries_.reserve(uniqueSourceIds_.size());
        for (uint32_t sourceId : uniqueSourceIds_)
        {
            auto sourceNode = scene_->GetNodeSharedByInstanceId(sourceId);
            if (!sourceNode)
            {
                continue;
            }

            const uint32_t newId = scene_->GenerateInstanceId();
            auto clone = CloneNode(*sourceNode, newId);

            std::shared_ptr<Assets::Node> parent;
            if (Assets::Node* sourceParent = sourceNode->GetParent())
            {
                parent = scene_->GetNodeSharedByInstanceId(sourceParent->GetInstanceId());
                if (parent)
                {
                    clone->SetParent(parent);
                }
            }

            scene_->AddNode(clone);
            duplicateEntries_.push_back(FDuplicateEntry{sourceId, newId, parent, clone});
            newInstanceIds_.push_back(newId);
        }
    }
    else
    {
        newInstanceIds_.reserve(duplicateEntries_.size());
        for (auto& entry : duplicateEntries_)
        {
            if (!entry.newNode)
            {
                continue;
            }

            if (entry.parent)
            {
                entry.newNode->SetParent(entry.parent);
            }

            scene_->AddNode(entry.newNode);
            newInstanceIds_.push_back(entry.newInstanceId);
        }
    }

    if (newInstanceIds_.empty())
    {
        return false;
    }

    scene_->SetSelection(newInstanceIds_);
    scene_->MarkDirty();
    return true;
}

bool DuplicateNodesCommand::Undo()
{
    if (scene_ == nullptr || duplicateEntries_.empty())
    {
        return false;
    }

    for (auto it = duplicateEntries_.rbegin(); it != duplicateEntries_.rend(); ++it)
    {
        scene_->RemoveNodeByInstanceId(it->newInstanceId);
    }

    RestoreSelection();
    scene_->MarkDirty();
    return true;
}

std::string DuplicateNodesCommand::GetDescription() const
{
    const size_t count = initialized_ ? uniqueSourceIds_.size() : sourceIds_.size();
    return count > 1 ? "Duplicate Nodes" : "Duplicate Node";
}

void DuplicateNodesCommand::InitializeSourceSelection()
{
    if (scene_ == nullptr)
    {
        return;
    }

    uniqueSourceIds_ = Runtime::Command::SelectionUtils::BuildUniqueValidSelection(*scene_, sourceIds_);
}

void DuplicateNodesCommand::RestoreSelection() const
{
    if (scene_ == nullptr)
    {
        return;
    }
    Runtime::Command::SelectionUtils::RestoreSelection(*scene_, previousSelection_, previousSelectedId_);
}

}
