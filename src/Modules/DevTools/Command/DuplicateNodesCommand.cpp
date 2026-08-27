#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SceneReferenceComponent.hpp"
#include "Modules/DevTools/Command/SelectionCommandUtils.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/SceneContent/SceneList.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

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
    bool duplicatedReference = false;
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

            std::shared_ptr<Assets::Node> parent;
            if (Assets::Node* sourceParent = sourceNode->GetParent())
            {
                parent = scene_->GetNodeSharedByInstanceId(sourceParent->GetInstanceId());
            }

            std::shared_ptr<Assets::Node> clone;
            if (auto sceneReference = sourceNode->GetComponent<Runtime::SceneReferenceComponent>())
            {
                clone = Runtime::Scene::SceneList::AddSceneReferenceToScene(
                    *scene_, sceneReference->GetAssetPath(), sourceNode->Translation());
                if (!clone)
                {
                    continue;
                }
                duplicatedReference = true;
                clone->SetName(sourceNode->GetName() + "_copy");
                clone->SetRotation(sourceNode->Rotation());
                clone->SetScale(sourceNode->Scale());
                clone->RecalcTransform();
                if (parent)
                {
                    clone->SetParent(parent);
                }
            }
            else
            {
                clone = CloneNode(*sourceNode, scene_->GenerateInstanceId());
                if (parent)
                {
                    clone->SetParent(parent);
                }
                scene_->AddNode(clone);
            }

            const uint32_t newId = clone->GetInstanceId();
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

            if (auto sceneReference = entry.newNode->GetComponent<Runtime::SceneReferenceComponent>())
            {
                auto recreated = Runtime::Scene::SceneList::AddSceneReferenceToScene(
                    *scene_, sceneReference->GetAssetPath(), entry.newNode->Translation());
                if (!recreated)
                {
                    continue;
                }
                duplicatedReference = true;
                recreated->SetName(entry.newNode->GetName());
                recreated->SetRotation(entry.newNode->Rotation());
                recreated->SetScale(entry.newNode->Scale());
                recreated->RecalcTransform();
                if (entry.parent)
                {
                    recreated->SetParent(entry.parent);
                }
                entry.newInstanceId = recreated->GetInstanceId();
                entry.newNode = recreated;
            }
            else
            {
                if (entry.parent)
                {
                    entry.newNode->SetParent(entry.parent);
                }

                scene_->AddNode(entry.newNode);
            }
            newInstanceIds_.push_back(entry.newInstanceId);
        }
    }

    if (newInstanceIds_.empty())
    {
        return false;
    }

    scene_->SetSelection(newInstanceIds_);
    scene_->MarkDirty();
    if (duplicatedReference)
    {
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            engine->RequestSceneGpuRefresh();
        }
    }
    return true;
}

bool DuplicateNodesCommand::Undo()
{
    if (scene_ == nullptr || duplicateEntries_.empty())
    {
        return false;
    }

    bool removedReference = false;
    for (auto it = duplicateEntries_.rbegin(); it != duplicateEntries_.rend(); ++it)
    {
        if (it->newNode && it->newNode->GetComponent<Runtime::SceneReferenceComponent>())
        {
            removedReference = true;
        }
        std::shared_ptr<Assets::Node> parent;
        scene_->RemoveNodeHierarchy(it->newInstanceId, parent);
    }

    RestoreSelection();
    scene_->MarkDirty();
    if (removedReference)
    {
        if (NextEngine* engine = NextEngine::GetInstance())
        {
            engine->RequestSceneGpuRefresh();
        }
    }
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
