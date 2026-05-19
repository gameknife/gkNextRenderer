#include "Engine/NextGameplay/Utilities/SceneNodeUtils.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Scene/NodeUtils.h"
#include "Engine/Runtime/Subsystems/NextPhysics.h"

namespace NextGameplay
{
    bool HasSkinnedMeshInHierarchy(const std::shared_ptr<Assets::Node>& node)
    {
        if (!node)
        {
            return false;
        }

        if (node->GetComponent<Runtime::SkinnedMeshComponent>())
        {
            return true;
        }

        for (const auto& child : node->Children())
        {
            if (HasSkinnedMeshInHierarchy(child))
            {
                return true;
            }
        }

        return false;
    }

    std::shared_ptr<Assets::Node> FindAppendedCharacterRoot(const Assets::Scene& scene, const std::string& baseName,
                                                            size_t ordinal, const std::shared_ptr<Assets::Node>& exclude)
    {
        std::vector<std::shared_ptr<Assets::Node>> candidates;
        for (const auto& node : scene.Nodes())
        {
            if (!node || node->GetParent() != nullptr)
            {
                continue;
            }

            const std::string& nodeName = node->GetName();
            if (nodeName != baseName && !nodeName.starts_with(baseName + "_"))
            {
                continue;
            }

            if (exclude && node->GetInstanceId() == exclude->GetInstanceId())
            {
                continue;
            }

            if (!HasSkinnedMeshInHierarchy(node))
            {
                continue;
            }

            candidates.push_back(node);
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const std::shared_ptr<Assets::Node>& lhs, const std::shared_ptr<Assets::Node>& rhs)
                  {
                      return lhs->GetInstanceId() < rhs->GetInstanceId();
                  });

        if (ordinal >= candidates.size())
        {
            return nullptr;
        }

        return candidates[ordinal];
    }

    std::vector<Runtime::SkinnedMeshComponent*> CollectSkinnedMeshComponents(
        const std::shared_ptr<Assets::Node>& root, Runtime::SkinnedMeshComponent** outPrimary)
    {
        std::vector<Runtime::SkinnedMeshComponent*> components;
        std::function<void(const std::shared_ptr<Assets::Node>&)> collectRecursive;
        collectRecursive = [&components, &collectRecursive](const std::shared_ptr<Assets::Node>& node)
        {
            if (!node)
            {
                return;
            }

            if (auto component = node->GetComponent<Runtime::SkinnedMeshComponent>())
            {
                components.push_back(component.get());
            }

            for (const auto& child : node->Children())
            {
                collectRecursive(child);
            }
        };

        collectRecursive(root);
        if (outPrimary)
        {
            *outPrimary = components.empty() ? nullptr : components.front();
        }

        return components;
    }

    void SetNodeVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        NodeUtils::SetVisibleRecursive(node, visible);
    }

    void SetNodeRayCastVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        NodeUtils::SetRayCastVisibleRecursive(node, visible);
    }

    void DisableNodePhysicsRecursive(const std::shared_ptr<Assets::Node>& node, NextPhysics* physicsEngine)
    {
        if (!node)
        {
            return;
        }

        if (auto physComp = node->GetComponent<Runtime::PhysicsComponent>())
        {
            if (physicsEngine)
            {
                const NextBodyID bodyId = physComp->GetPhysicsBody();
                if (!bodyId.IsInvalid())
                {
                    physicsEngine->RemoveBody(bodyId);
                }
            }

            auto disabledPhysicsComp = std::make_shared<Runtime::PhysicsComponent>();
            disabledPhysicsComp->SetMobility(Runtime::ENodeMobility::Dynamic);
            disabledPhysicsComp->SetPhysicsOffset(physComp->GetPhysicsOffset());
            node->AddComponent(disabledPhysicsComp);
        }

        for (const auto& child : node->Children())
        {
            DisableNodePhysicsRecursive(child, physicsEngine);
        }
    }
}
