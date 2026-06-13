#pragma once

#include "Gameplay/Rig/RigInstance.h"
#include "Gameplay/Sim/SimVisual.h"

#include <memory>

namespace NextGameplay::Sim
{
    struct FRigVisualParams
    {
        float baseWalkSpeed = 1.8f;
        float sizeJitterRange = 0.05f;
        glm::vec3 parkedPosition{0.0f, -100.0f, 0.0f};
    };

    class FScadRigVisual final : public ISimVisual
    {
    public:
        FScadRigVisual(Assets::Scene& scene, const Assets::FRigAsset& asset,
                       const NextGameplay::FRigInstanceDesc& desc, int poolSlot,
                       const FRigVisualParams& params = {});

        void SetWorldTransform(const glm::vec3& position, float yaw) override;
        void SetAnimHint(EAnimHint hint) override;
        void SetMoveSpeed(float metersPerSecond) override;
        void SetVisible(bool visible) override;
        void Tick(float deltaSeconds) override;

        static const char* ClipName(EAnimHint hint);

    private:
        const Assets::FRigAsset* asset_ = nullptr;
        std::shared_ptr<Assets::Node> worldNode_;
        NextGameplay::FRigAnimator animator_;
        FRigVisualParams params_;
        EAnimHint hint_ = EAnimHint::Idle;
    };
}
