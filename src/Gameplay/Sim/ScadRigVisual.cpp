#include "ScadRigVisual.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"

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

        auto rigRoot = NextGameplay::FRigInstance::Instantiate(scene, asset, desc, boneNodes_);
        if (rigRoot)
        {
            rigRoot->SetParent(worldNode_);
            worldNode_->RecalcTransform(true);
        }
        animator_.Bind(asset_, boneNodes_, worldNode_.get());
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

    void FScadRigVisual::PlayClip(std::string_view clip, float fadeSeconds)
    {
        // Deliberately leaves the hint alone. A consumer drives this visual either by hint or by
        // clip name; interleaving the two would need a "no hint" state that the shared enum does
        // not have, and inventing one would change what SetAnimHint means for every existing
        // consumer. A game with more states than the four hints uses this exclusively.
        animator_.Play(clip, fadeSeconds);
    }

    const std::string& FScadRigVisual::CurrentClip() const { return animator_.CurrentClip(); }

    void FScadRigVisual::SetPlaySpeed(float speed) { animator_.SetPlaySpeed(speed); }

    int32_t FScadRigVisual::RootNodeId() const
    {
        return worldNode_ ? static_cast<int32_t>(worldNode_->GetInstanceId()) : -1;
    }

    int32_t FScadRigVisual::BoneNodeId(std::string_view boneName) const
    {
        if (asset_ == nullptr)
        {
            return -1;
        }
        const int32_t bone = asset_->FindBone(boneName);
        if (bone < 0 || static_cast<size_t>(bone) >= boneNodes_.size() || boneNodes_[bone] == nullptr)
        {
            return -1;
        }
        return static_cast<int32_t>(boneNodes_[bone]->GetInstanceId());
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
