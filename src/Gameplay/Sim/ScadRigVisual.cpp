#include "ScadRigVisual.h"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.h"

#include <glm/gtc/quaternion.hpp>

namespace NextGameplay::Sim
{
    const char* FScadRigVisual::ClipName(EAnimHint hint)
    {
        switch (hint)
        {
        case EAnimHint::Walk: return "walk";
        case EAnimHint::Sit: return "sit";
        case EAnimHint::Work: return "work";
        case EAnimHint::Idle:
        default: return "idle";
        }
    }

    FScadRigVisual::FScadRigVisual(Assets::Scene& scene, const Assets::FRigAsset& asset,
                                   const NextGameplay::FRigInstanceDesc& desc, int poolSlot,
                                   const FRigVisualParams& params)
        : asset_(&asset), params_(params)
    {
        const float fraction = static_cast<float>((poolSlot * 7) % 11) / 10.0f;
        const float sizeJitter = 1.0f + (fraction * 2.0f - 1.0f) * params_.sizeJitterRange;
        worldNode_ = Assets::Node::CreateNode(
            desc.namePrefix, params_.parkedPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec3(sizeJitter), scene.GenerateInstanceId());
        auto physics = std::make_shared<Runtime::PhysicsComponent>();
        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
        worldNode_->AddComponent(physics);
        scene.AddNode(worldNode_);

        std::vector<Assets::Node*> boneNodes;
        auto rigRoot = NextGameplay::FRigInstance::Instantiate(scene, asset, desc, boneNodes);
        if (rigRoot)
        {
            rigRoot->SetParent(worldNode_);
            worldNode_->RecalcTransform(true);
        }
        animator_.Bind(asset_, std::move(boneNodes), worldNode_.get());
        animator_.SetPhaseOffset(static_cast<float>(poolSlot) * 0.37f);
        animator_.Play(asset.FindClip("idle") != nullptr ? "idle" : "", 0.0f);
    }

    void FScadRigVisual::SetWorldTransform(const glm::vec3& position, float yaw)
    {
        worldNode_->Translation() = position;
        worldNode_->Rotation() = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        worldNode_->RecalcTransform(true);
    }

    void FScadRigVisual::SetAnimHint(EAnimHint hint)
    {
        if (hint == hint_)
        {
            return;
        }
        hint_ = hint;
        if (hint != EAnimHint::Walk)
        {
            animator_.SetPlaySpeed(1.0f);
        }
        const char* wanted = ClipName(hint);
        animator_.Play(asset_->FindClip(wanted) != nullptr ? wanted : "idle");
    }

    void FScadRigVisual::SetMoveSpeed(float metersPerSecond)
    {
        if (hint_ == EAnimHint::Walk && params_.baseWalkSpeed > 0.0f)
        {
            animator_.SetPlaySpeed(metersPerSecond / params_.baseWalkSpeed);
        }
    }

    void FScadRigVisual::SetVisible(bool visible)
    {
        if (!visible)
        {
            SetWorldTransform(params_.parkedPosition, 0.0f);
        }
    }

    void FScadRigVisual::Tick(float deltaSeconds)
    {
        animator_.Update(deltaSeconds);
    }
}
