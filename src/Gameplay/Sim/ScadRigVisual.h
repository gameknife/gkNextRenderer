#pragma once

#include "Gameplay/Rig/RigInstance.h"
#include "Gameplay/Sim/SimVisual.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

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

        /// Plays a clip by name, for gameplay that has more than the four hints. A shooter picks
        /// between aim, recoil and reload; none of those is expressible as a hint, and inventing
        /// hints for them would put game vocabulary into the shared sim layer.
        ///
        /// A name the asset does not have animates to the bind pose — ask the asset first when the
        /// name comes from data rather than from a literal. Do not interleave with SetAnimHint:
        /// a visual is driven by one or the other.
        void PlayClip(std::string_view clip, float fadeSeconds = 0.15f);
        const std::string& CurrentClip() const;

        /// Animator playback rate, for gameplay that already knows how fast the clip should run.
        /// SetMoveSpeed is the other way round: it derives the rate from a walk speed, and only
        /// while the Walk hint is active.
        void SetPlaySpeed(float speed);

        /// Scene node ids, so a caller can hang something off a bone (a held weapon, a muzzle
        /// flash) or address the character's world node. -1 when there is no such bone.
        int32_t RootNodeId() const;
        int32_t BoneNodeId(std::string_view boneName) const;
        void SetMoveSpeed(float metersPerSecond) override;
        void SetVisible(bool visible) override;
        void Tick(float deltaSeconds) override;

        static const char* ClipName(EAnimHint hint);

    private:
        const Assets::FRigAsset* asset_ = nullptr;
        std::shared_ptr<Assets::Node> worldNode_;
        /// Aligned with asset_->bones. Kept so a bone can be addressed by name after construction;
        /// the animator holds the same pointers but owns them for its own use.
        std::vector<Assets::Node*> boneNodes_;
        NextGameplay::FRigAnimator animator_;
        FRigVisualParams params_;
        EAnimHint hint_ = EAnimHint::Idle;
    };
}
