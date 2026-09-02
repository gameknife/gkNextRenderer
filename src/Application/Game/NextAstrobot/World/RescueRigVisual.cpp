#include "Application/Game/NextAstrobot/World/RescueRigVisual.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Scene/NodeUtils.hpp"

#include <fmt/format.h>

#include "Application/Game/NextAstrobot/Mechanisms/MechanismCurves.hpp"

namespace NextAstrobot
{
    namespace
    {
        // Parked below the level until a rescue calls one up, the same trick the player
        // rig uses before its first Update.
        constexpr float kParkedY = -1000.0f;
        // How fast a freed robot steps clear of the cage, and the bounds on how long that
        // step is allowed to take: too quick and it is the teleport again, too slow and the
        // close-up is over before it arrives.
        constexpr float kEmergeSpeed = 1.6f;
        constexpr float kEmergeMinSeconds = 0.4f;
        constexpr float kEmergeMaxSeconds = 1.1f;
        // The run cycle is authored for 6 m/s; a walk-out plays it slowed down.
        constexpr float kEmergePlaySpeed = 0.55f;
        // Used when the rig ships no cheer clip, so the timeline still makes sense.
        constexpr float kCheerFallbackSeconds = 1.6f;
        // The turn back toward the player happens over the front of the cheer rather than
        // on the frame it starts, which is what the old instant Place looked like.
        constexpr float kFaceTurnSeconds = 0.35f;

        float WrapAngle(float radians)
        {
            while (radians > kPi) radians -= 2.0f * kPi;
            while (radians < -kPi) radians += 2.0f * kPi;
            return radians;
        }

        /// Shortest-way interpolation between two headings.
        float LerpAngle(float from, float to, float t)
        {
            return from + WrapAngle(to - from) * std::clamp(t, 0.0f, 1.0f);
        }
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
            // Rescued robots never hover, so their thruster beams stay off for good;
            // left on they are two 2 m spikes through the floor under every freed robot.
            for (const char* jet : {"bone_jet_l", "bone_jet_r"})
            {
                const int32_t bone = asset.FindBone(jet);
                if (bone >= 0 && bone < static_cast<int32_t>(instance.boneNodes.size()) &&
                    instance.boneNodes[bone])
                {
                    instance.jetBones.push_back(instance.boneNodes[bone]);
                }
            }
            HideJets(instance);
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

    void FRescueRigVisual::HideJets(FInstance& instance) const
    {
        for (Assets::Node* jet : instance.jetBones)
        {
            Assets::NodeUtils::SetVisibleRecursive(jet->shared_from_this(), false);
        }
    }

    void FRescueRigVisual::Pose(FInstance& instance, const glm::vec3& footPosition, float yaw) const
    {
        instance.worldNode->SetTransform(footPosition, glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)),
                                         instance.worldNode->Scale());
    }

    float FRescueRigVisual::Place(const glm::vec3& fromFoot, const glm::vec3& toFoot, float faceYaw)
    {
        for (FInstance& instance : instances_)
        {
            if (instance.phase != EPhase::Free || !instance.worldNode)
            {
                continue;
            }
            const glm::vec3 step = toFoot - fromFoot;
            const float distance = std::sqrt(step.x * step.x + step.z * step.z);

            instance.phase = EPhase::Emerge;
            instance.from = fromFoot;
            instance.to = toFoot;
            instance.phaseTime = 0.0f;
            instance.phaseDuration =
                std::clamp(distance / kEmergeSpeed, kEmergeMinSeconds, kEmergeMaxSeconds);
            // Below the threshold the step is too short to read as a direction, so the
            // robot simply walks on the spot facing where it will end up.
            instance.travelYaw = distance > 0.05f ? std::atan2(step.x, step.z) : faceYaw;
            instance.faceYaw = faceYaw;

            Pose(instance, fromFoot, instance.travelYaw);
            Assets::NodeUtils::SetVisibleRecursive(instance.worldNode, true);
            // Showing the instance is recursive, so the beams have to go back off after it.
            HideJets(instance);
            instance.animator.Play("run", 0.0f);
            instance.animator.SetPlaySpeed(kEmergePlaySpeed);
            ++placed_;

            const Assets::FRigClip* cheer = asset_ ? asset_->FindClip("cheer") : nullptr;
            const float cheerSeconds = cheer && cheer->duration > 0.0f ? cheer->duration : kCheerFallbackSeconds;
            return instance.phaseDuration + cheerSeconds;
        }
        return 0.0f;
    }

    void FRescueRigVisual::Update(float deltaSeconds)
    {
        for (FInstance& instance : instances_)
        {
            if (instance.phase == EPhase::Free || !instance.worldNode)
            {
                continue;
            }

            instance.phaseTime += deltaSeconds;
            switch (instance.phase)
            {
            case EPhase::Emerge:
            {
                const float t = instance.phaseDuration > 0.0f
                                    ? Smoothstep01(instance.phaseTime / instance.phaseDuration)
                                    : 1.0f;
                Pose(instance, glm::mix(instance.from, instance.to, t), instance.travelYaw);
                if (instance.phaseTime >= instance.phaseDuration)
                {
                    instance.phase = EPhase::Cheer;
                    instance.phaseTime = 0.0f;
                    const Assets::FRigClip* cheer = asset_ ? asset_->FindClip("cheer") : nullptr;
                    instance.phaseDuration =
                        cheer && cheer->duration > 0.0f ? cheer->duration : kCheerFallbackSeconds;
                    instance.animator.SetPlaySpeed(1.0f);
                    instance.animator.Play("cheer", 0.12f);
                }
                break;
            }
            case EPhase::Cheer:
                Pose(instance, instance.to,
                     LerpAngle(instance.travelYaw, instance.faceYaw, instance.phaseTime / kFaceTurnSeconds));
                if (instance.phaseTime >= instance.phaseDuration)
                {
                    instance.phase = EPhase::Wave;
                    instance.phaseTime = 0.0f;
                    instance.animator.Play("wave", 0.25f);
                }
                break;
            case EPhase::Wave:
            case EPhase::Free:
                break;
            }

            instance.animator.Update(deltaSeconds);
        }
    }
}
