#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Gameplay/Components/CharacterAnimationComponent.h"
#include "Gameplay/Components/CharacterControlComponent.h"
#include "Gameplay/Components/CharacterGameplayComponent.h"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Gameplay/Character/NextCharacterController.h"

namespace NextGameplay
{
    struct FCharacterActorSetup
    {
        std::string actorName = "CharacterActor";
        std::string visualName = "CharacterBody";
        glm::vec3 initialPosition{0.0f};
        std::string appendRootName = "Mannequin_Medium";
        ECharacterControlSource controlSource = ECharacterControlSource::Player;
        ECharacterMovementMode movementMode = ECharacterMovementMode::CameraAligned;
        bool firstPersonMode = false;
        float eyeHeight = 1.55f;
        float facingYaw = 0.0f;
        float walkStrafePlaySpeed = 0.82f;
        float runBackwardPlaySpeed = 1.35f;
        float jumpStartHoldTime = 0.12f;
        float jumpLandHoldTime = 0.18f;
    };

    class CharacterActor
    {
    public:
        void Reset();
        void CreateController(NextPhysics* physics, const FCharacterControllerSettings& settings);
        void Initialize(Assets::Scene& scene, const FCharacterActorSetup& setup);
        std::shared_ptr<Assets::Node> CreatePlaceholderVisual(Assets::Scene& scene, const std::string& visualName,
                                                              uint32_t modelId, uint32_t materialId);
        void SetModelLoadRequested(bool requested);
        void SetModelLoaded(bool loaded);
        void SetFirstPersonMode(bool enabled);
        void SetMovementMode(ECharacterMovementMode mode);
        void SetFacingYaw(float yaw);
        void ClearControlFrameState() const;
        void SetControlIntent(const glm::vec3& moveIntent, const glm::vec3& lookIntent, float desiredSpeed,
                              bool sprinting, bool jumpRequested) const;
        bool ConsumeJumpRequested() const;
        void SyncTransform(const glm::vec3& position, float yaw);
        bool TryResolveSkinnedModel(Assets::Scene& scene, const std::string& baseName, size_t ordinal,
                                    const std::shared_ptr<Assets::Node>& excludeRoot = nullptr);
        void AttachSkinnedModel(NextPhysics* physicsEngine);
        void RemoveVisualRoot(Assets::Scene& scene, NextPhysics* physicsEngine);
        bool UpdateAnimation(const FCharacterAnimationUpdateInput& input) const;
        void PlayAnimation(const std::string& name, bool loop, float playSpeed = 1.0f) const;
        bool IsAnimationReady() const;
        Runtime::SkinnedMeshComponent* GetPrimarySkinnedMeshComponent() const { return primarySkinnedMeshComp; }

        NextCharacterController controller;
        std::shared_ptr<Assets::Node> actorRoot;
        std::shared_ptr<CharacterGameplayComponent> gameplay;
        std::shared_ptr<CharacterAnimationComponent> animation;
        std::shared_ptr<CharacterControlComponent> control;
        std::shared_ptr<Assets::Node> skinnedRoot;
        Runtime::SkinnedMeshComponent* primarySkinnedMeshComp = nullptr;
        std::vector<Runtime::SkinnedMeshComponent*> skinnedMeshComps;
        std::string appendRootName = "Mannequin_Medium";
        bool modelLoaded = false;
        bool modelLoadRequested = false;
    };
}
