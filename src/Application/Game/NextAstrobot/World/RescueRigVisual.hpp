#pragma once

// ============================================================================
// RescueRigVisual.hpp - A small pool of astro_bot rig instances that stand in
// for rescued robots. The kit's ab_char_bot / ab_char_bot_lost are static
// geometry: the moment the player frees one, that geometry is hidden and one of
// these takes its place. The swap has to be continuous to read as a rescue, so
// the instance appears exactly where the static prop stood and then plays out
// walk-out -> one-shot cheer -> looping wave under its own clock. The pool
// shares the player's rig asset and its injected models, so freeing a robot
// costs a node tree and an animator, not another copy of the mesh data.
// ============================================================================

#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Assets/Data/RigAsset.hpp"
#include "Gameplay/Rig/RigInstance.h"

namespace Assets
{
    class Node;
    class Scene;
}

namespace NextAstrobot
{
    class FRescueRigVisual
    {
    public:
        /// `count` is how many robots the level can free; each gets its own parked
        /// instance up front so a rescue never allocates mid-frame. `tintMaterialIds`
        /// cycles per instance, so a group of freed robots is not a row of clones.
        void Create(Assets::Scene& scene, const Assets::FRigAsset& asset,
                    const NextGameplay::FRigInstanceDesc& baseDesc,
                    const std::vector<uint32_t>& tintMaterialIds, int count);
        void Destroy();

        /// Stands the next free instance where the static prop was and walks it out to
        /// `toFoot`, where it cheers once and then settles into a wave facing `faceYaw`.
        /// Returns how long that performance lasts, in seconds, so the caller can hold a
        /// close-up on it for exactly as long; 0 means the pool was exhausted, which only
        /// happens if the level frees more robots than the index counted.
        float Place(const glm::vec3& fromFoot, const glm::vec3& toFoot, float faceYaw);
        void Update(float deltaSeconds);

        int PlacedCount() const { return placed_; }
        size_t Size() const { return instances_.size(); }

    private:
        /// Walk out of the cage, cheer once, then wave forever. Free = parked below the
        /// level, which is where every instance starts.
        enum class EPhase : uint8_t
        {
            Free,
            Emerge,
            Cheer,
            Wave,
        };

        struct FInstance
        {
            std::shared_ptr<Assets::Node> worldNode;
            std::vector<Assets::Node*> boneNodes;
            /// The two thruster beams, kept for re-hiding: showing the instance is a
            /// recursive call and would otherwise light them back up.
            std::vector<Assets::Node*> jetBones;
            NextGameplay::FRigAnimator animator;
            EPhase phase = EPhase::Free;
            glm::vec3 from{0.0f};
            glm::vec3 to{0.0f};
            float phaseTime = 0.0f;
            float phaseDuration = 0.0f;
            /// Heading while stepping out (the direction of travel) and the one it turns
            /// to for the cheer, which is back toward whoever let it out.
            float travelYaw = 0.0f;
            float faceYaw = 0.0f;
        };

        void HideJets(FInstance& instance) const;
        void Pose(FInstance& instance, const glm::vec3& footPosition, float yaw) const;

        const Assets::FRigAsset* asset_ = nullptr;
        std::vector<FInstance> instances_;
        int placed_ = 0;
    };
}
