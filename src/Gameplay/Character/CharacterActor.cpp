#include "Gameplay/Character/CharacterActor.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Gameplay/Utilities/SceneNodeUtils.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

namespace NextGameplay
{
    void CharacterActor::Reset()
    {
        controller.Destroy();
        actorRoot.reset();
        gameplay.reset();
        animation.reset();
        control.reset();
        skinnedRoot.reset();
        primarySkinnedMeshComp = nullptr;
        skinnedMeshComps.clear();
        appendRootName = "Mannequin_Medium";
        modelLoaded = false;
        modelLoadRequested = false;
    }

    void CharacterActor::CreateController(NextPhysics* physics, const FCharacterControllerSettings& settings)
    {
        controller.Destroy();
        controller.Create(physics, settings);
    }

    void CharacterActor::Initialize(Assets::Scene& scene, const FCharacterActorSetup& setup)
    {
        gameplay = std::make_shared<CharacterGameplayComponent>();
        animation = std::make_shared<CharacterAnimationComponent>();
        control = std::make_shared<CharacterControlComponent>();

        gameplay->SetFirstPersonMode(setup.firstPersonMode);
        gameplay->SetMovementMode(setup.movementMode);
        gameplay->SetEyeHeight(setup.eyeHeight);
        gameplay->facingYaw = setup.facingYaw;

        appendRootName = setup.appendRootName;
        gameplay->appendRootName = appendRootName;
        modelLoaded = false;
        modelLoadRequested = false;
        gameplay->modelLoaded = false;
        gameplay->modelLoadRequested = false;

        control->SetControlSource(setup.controlSource);
        animation->SetBlendTuning(setup.walkStrafePlaySpeed, setup.runBackwardPlaySpeed, setup.jumpStartHoldTime,
                                  setup.jumpLandHoldTime);

        const uint32_t actorRootId = scene.GenerateInstanceId();
        actorRoot = Assets::Node::CreateNode(setup.actorName, setup.initialPosition, glm::quat(1, 0, 0, 0),
                                             glm::vec3(1.0f), actorRootId);
        actorRoot->AddComponent(gameplay);
        actorRoot->AddComponent(animation);
        actorRoot->AddComponent(control);
        scene.AddNode(actorRoot);
    }

    std::shared_ptr<Assets::Node> CharacterActor::CreatePlaceholderVisual(Assets::Scene& scene,
                                                                          const std::string& visualName,
                                                                          uint32_t modelId, uint32_t materialId)
    {
        if (!actorRoot)
        {
            return nullptr;
        }

        const uint32_t instanceId = scene.GenerateInstanceId();
        auto visualRoot = Assets::SceneBuilder::CreateRenderNode(visualName,
                                                         glm::vec3(0.0f),
                                                         glm::vec3(1.0f),
                                                         instanceId,
                                                         modelId,
                                                         materialId,
                                                         true,
                                                         glm::quat(1, 0, 0, 0),
                                                         false);
        visualRoot->SetParent(actorRoot);

        auto physicsComp = std::make_shared<Runtime::PhysicsComponent>();
        physicsComp->SetMobility(Runtime::ENodeMobility::Dynamic);
        visualRoot->AddComponent(physicsComp);

        if (gameplay)
        {
            gameplay->visualRoot = visualRoot;
        }

        scene.AddNode(visualRoot);
        return visualRoot;
    }

    void CharacterActor::SetModelLoadRequested(bool requested)
    {
        modelLoadRequested = requested;
        if (gameplay)
        {
            gameplay->modelLoadRequested = requested;
        }
    }

    void CharacterActor::SetModelLoaded(bool loaded)
    {
        modelLoaded = loaded;
        if (gameplay)
        {
            gameplay->modelLoaded = loaded;
        }
    }

    void CharacterActor::SetFirstPersonMode(bool enabled)
    {
        if (gameplay)
        {
            gameplay->SetFirstPersonMode(enabled);
        }
    }

    void CharacterActor::SetMovementMode(ECharacterMovementMode mode)
    {
        if (gameplay)
        {
            gameplay->SetMovementMode(mode);
        }
    }

    void CharacterActor::SetFacingYaw(float yaw)
    {
        if (gameplay)
        {
            gameplay->facingYaw = yaw;
        }
    }

    void CharacterActor::ClearControlFrameState() const
    {
        if (control)
        {
            control->ClearFrameState();
        }
    }

    void CharacterActor::SetControlIntent(const glm::vec3& moveIntent, const glm::vec3& lookIntent, float desiredSpeed,
                                          bool sprinting, bool jumpRequested) const
    {
        if (!control)
        {
            return;
        }

        control->SetMoveIntent(moveIntent);
        control->SetLookIntent(lookIntent);
        control->SetDesiredSpeed(desiredSpeed);
        control->SetSprinting(sprinting);
        control->SetJumpRequested(jumpRequested);
    }

    bool CharacterActor::ConsumeJumpRequested() const
    {
        return control ? control->ConsumeJumpRequested() : false;
    }

    void CharacterActor::SyncTransform(const glm::vec3& position, float yaw)
    {
        SetFacingYaw(yaw);
        if (!actorRoot)
        {
            return;
        }

        actorRoot->SetTranslation(position);
        actorRoot->SetRotation(glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
        actorRoot->RecalcTransform(true);
    }

    bool CharacterActor::TryResolveSkinnedModel(Assets::Scene& scene, const std::string& baseName, size_t ordinal,
                                                const std::shared_ptr<Assets::Node>& excludeRoot)
    {
        if (!skinnedRoot)
        {
            skinnedRoot = FindAppendedCharacterRoot(scene, baseName, ordinal, excludeRoot);
            if (skinnedRoot)
            {
                appendRootName = skinnedRoot->GetName();
                if (gameplay)
                {
                    gameplay->appendRootName = appendRootName;
                }
            }
        }

        if (!skinnedRoot)
        {
            return false;
        }

        skinnedMeshComps = CollectSkinnedMeshComponents(skinnedRoot, &primarySkinnedMeshComp);
        if (gameplay)
        {
            gameplay->SetSkinnedMeshComponents(skinnedMeshComps);
            gameplay->skinnedRoot = skinnedRoot;
        }
        return !skinnedMeshComps.empty();
    }

    void CharacterActor::AttachSkinnedModel(NextPhysics* physicsEngine)
    {
        if (!skinnedRoot)
        {
            return;
        }

        if (actorRoot)
        {
            skinnedRoot->SetTranslation(glm::vec3(0.0f));
            skinnedRoot->SetRotation(glm::quat(1, 0, 0, 0));
            skinnedRoot->SetParent(actorRoot);
        }

        DisableNodePhysicsRecursive(skinnedRoot, physicsEngine);
        SetNodeRayCastVisibilityRecursive(skinnedRoot, false);
    }

    void CharacterActor::RemoveVisualRoot(Assets::Scene& scene, NextPhysics* physicsEngine)
    {
        if (!gameplay || !gameplay->visualRoot)
        {
            return;
        }

        DisableNodePhysicsRecursive(gameplay->visualRoot, physicsEngine);
        scene.RemoveNodeByInstanceId(gameplay->visualRoot->GetInstanceId());
        gameplay->visualRoot.reset();
    }

    bool CharacterActor::UpdateAnimation(const FCharacterAnimationUpdateInput& input) const
    {
        if (!gameplay || !animation || !control || skinnedMeshComps.empty())
        {
            return false;
        }

        return animation->UpdateAnimation(*gameplay, input);
    }

    void CharacterActor::PlayAnimation(const std::string& name, bool loop, float playSpeed) const
    {
        if (gameplay)
        {
            gameplay->PlayAnimation(name, loop, playSpeed);
        }
    }

    bool CharacterActor::IsAnimationReady() const
    {
        return gameplay != nullptr && animation != nullptr && control != nullptr && !skinnedMeshComps.empty();
    }
}
