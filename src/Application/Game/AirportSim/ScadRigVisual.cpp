#include "ScadRigVisual.h"

#include "AirportSimConfig.hpp"

#include <fmt/format.h>

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.h"

namespace AirportSim
{
    namespace
    {
        const char* ClipForHint(const Assets::FRigAsset& asset, EAgentAnimHint hint)
        {
            const char* wanted = "idle";
            switch (hint)
            {
            case EAgentAnimHint::Idle: wanted = "idle"; break;
            case EAgentAnimHint::Walk: wanted = "walk"; break;
            case EAgentAnimHint::Sit:  wanted = "sit";  break;
            case EAgentAnimHint::Work: wanted = "work"; break;
            }
            return asset.FindClip(wanted) ? wanted : "idle";
        }
    }

    ScadRigVisual::ScadRigVisual(Assets::Scene& scene,
                                 const Assets::FRigAsset& asset,
                                 const NextGameplay::FRigInstanceDesc& desc,
                                 int poolSlot)
        : asset_(&asset)
    {
        // 体型微差异（§5.2）：0.95 ~ 1.05 均匀缩放。
        const float sizeJitter = 0.95f + 0.10f * (static_cast<float>((poolSlot * 7) % 11) / 10.0f);

        worldNode_ = Assets::Node::CreateNode(
            desc.namePrefix, Config::kParkedPos, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
            glm::vec3(sizeJitter), scene.GenerateInstanceId());
        auto phys = std::make_shared<Runtime::PhysicsComponent>();
        phys->SetMobility(Runtime::ENodeMobility::Dynamic);
        worldNode_->AddComponent(phys);
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
        animator_.Play(ClipForHint(asset, EAgentAnimHint::Idle), 0.0f);
    }

    void ScadRigVisual::SetWorldTransform(const glm::vec3& pos, float yaw)
    {
        worldNode_->Translation() = pos;
        worldNode_->Rotation() = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        worldNode_->RecalcTransform(true);
    }

    void ScadRigVisual::SetAnimHint(EAgentAnimHint hint)
    {
        if (hint == hint_)
        {
            return;
        }
        hint_ = hint;
        if (hint != EAgentAnimHint::Walk)
        {
            animator_.SetPlaySpeed(1.0f);
        }
        animator_.Play(ClipForHint(*asset_, hint));
    }

    void ScadRigVisual::SetMoveSpeed(float metersPerSecond)
    {
        if (hint_ == EAgentAnimHint::Walk && Config::kBaseWalkSpeed > 0.0f)
        {
            animator_.SetPlaySpeed(metersPerSecond / Config::kBaseWalkSpeed);
        }
    }

    void ScadRigVisual::SetVisible(bool visible)
    {
        if (!visible)
        {
            SetWorldTransform(Config::kParkedPos, 0.0f);
        }
    }

    void ScadRigVisual::Tick(float deltaSeconds)
    {
        animator_.Update(deltaSeconds);
    }
} // namespace AirportSim
