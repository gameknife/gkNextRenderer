#pragma once

// ============================================================================
// ScadRigVisual.h - IAgentVisual backed by a ScadRig character (see
// docs/ScadRig-Design.md §5): a world node carries the agent transform and
// physics; the rig bone tree hangs under it and FRigAnimator plays the
// idle/walk/sit/work clips. Game logic keeps talking to IAgentVisual only.
// ============================================================================

#include "AgentSystem.h"

#include "Gameplay/Rig/RigInstance.h"

#include <memory>

namespace AirportSim
{
    class ScadRigVisual final : public IAgentVisual
    {
    public:
        // poolSlot drives the phase offset and body-size variation so the 28
        // pool agents never animate in lockstep.
        ScadRigVisual(Assets::Scene& scene,
                      const Assets::FRigAsset& asset,
                      const NextGameplay::FRigInstanceDesc& desc,
                      int poolSlot);

        void SetWorldTransform(const glm::vec3& pos, float yaw) override;
        void SetAnimHint(EAgentAnimHint hint) override;
        void SetMoveSpeed(float metersPerSecond) override;
        void SetVisible(bool visible) override;
        void Tick(float deltaSeconds) override;

    private:
        const Assets::FRigAsset* asset_ = nullptr;
        std::shared_ptr<Assets::Node> worldNode_;
        NextGameplay::FRigAnimator animator_;
        EAgentAnimHint hint_ = EAgentAnimHint::Idle;
    };
} // namespace AirportSim
