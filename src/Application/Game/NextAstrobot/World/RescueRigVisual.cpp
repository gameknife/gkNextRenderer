#include "Application/Game/NextAstrobot/World/RescueRigVisual.hpp"

#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

#include <fmt/format.h>

namespace NextAstrobot
{
    namespace
    {
        // Parked below the level until a rescue calls one up, the same trick the player
        // rig uses before its first Update.
        constexpr float kParkedY = -1000.0f;
    }

    void FRescueRigVisual::Create(Assets::Scene& scene, const Assets::FRigAsset& asset,
                                  const NextGameplay::FRigInstanceDesc& baseDesc,
                                  const std::vector<uint32_t>& tintMaterialIds, int count)
    {
        Destroy();
        if (count <= 0 || asset.bones.empty())
        {
            return;
        }
        asset_ = &asset;
        instances_.resize(static_cast<size_t>(count));

        for (size_t index = 0; index < instances_.size(); ++index)
        {
            FInstance& instance = instances_[index];
            instance.worldNode = Assets::Node::CreateNode(fmt::format("astro_rescue_{}", index),
                                                          glm::vec3(0.0f, kParkedY, 0.0f),
                                                          glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f),
                                                          scene.GenerateInstanceId());
            // Dynamic mobility re-uploads the subtree transform every frame without
            // binding a collider: these are pure visuals standing in a cleared cage.
            auto physics = std::make_shared<Runtime::PhysicsComponent>();
            physics->SetMobility(Runtime::ENodeMobility::Dynamic);
            instance.worldNode->AddComponent(physics);
            scene.AddNode(instance.worldNode);

            NextGameplay::FRigInstanceDesc desc = baseDesc;
            desc.namePrefix = fmt::format("astro_rescue_{}", index);
            if (!tintMaterialIds.empty())
            {
                const uint32_t tint = tintMaterialIds[index % tintMaterialIds.size()];
                for (size_t partIndex = 0; partIndex < asset.parts.size() && partIndex < desc.partMaterialIds.size();
                     ++partIndex)
                {
                    const Assets::FRigPart& part = asset.parts[partIndex];
                    for (size_t section = 0; section < part.sectionTintable.size() && section < 16; ++section)
                    {
                        if (part.sectionTintable[section])
                        {
                            desc.partMaterialIds[partIndex][section] = tint;
                        }
                    }
                }
            }

            std::shared_ptr<Assets::Node> rigRoot =
                NextGameplay::FRigInstance::Instantiate(scene, asset, desc, instance.boneNodes);
            if (!rigRoot)
            {
                continue;
            }
            rigRoot->SetParent(instance.worldNode);
            Assets::NodeUtils::SetRayCastVisibleRecursive(instance.worldNode, false);
            // Hidden while parked: a rig sitting a kilometre below the level still shows
            // up in a reflection or a wide shot otherwise.
            Assets::NodeUtils::SetVisibleRecursive(instance.worldNode, false);
            instance.animator.Bind(&asset, instance.boneNodes, rigRoot.get());
            instance.animator.Play("idle", 0.0f);
            // Stagger the clips so a pair of freed robots does not wave in lockstep.
            instance.animator.SetPhaseOffset(static_cast<float>(index) * 0.37f);
        }
    }

    void FRescueRigVisual::Destroy()
    {
        // Runtime pointers only: the nodes belong to the scene that is being torn down.
        instances_.clear();
        asset_ = nullptr;
        placed_ = 0;
    }

    bool FRescueRigVisual::Place(const glm::vec3& footPosition, float yaw, std::string_view clip)
    {
        for (FInstance& instance : instances_)
        {
            if (instance.active || !instance.worldNode)
            {
                continue;
            }
            instance.active = true;
            instance.worldNode->SetTransform(footPosition, glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)),
                                             instance.worldNode->Scale());
            Assets::NodeUtils::SetVisibleRecursive(instance.worldNode, true);
            instance.animator.Play(clip, 0.0f);
            ++placed_;
            return true;
        }
        return false;
    }

    void FRescueRigVisual::Update(float deltaSeconds)
    {
        for (FInstance& instance : instances_)
        {
            if (instance.active)
            {
                instance.animator.Update(deltaSeconds);
            }
        }
    }
}
