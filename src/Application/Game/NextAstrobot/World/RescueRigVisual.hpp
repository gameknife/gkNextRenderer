#pragma once

// ============================================================================
// RescueRigVisual.hpp - A small pool of astro_bot rig instances that stand in
// for rescued robots. The kit's ab_char_bot / ab_char_bot_lost are static
// geometry: the moment the player frees one, that geometry is hidden and one of
// these takes its place, waving or cheering. The pool shares the player's rig
// asset and its injected models, so freeing a robot costs a node tree and an
// animator, not another copy of the mesh data.
// ============================================================================

#include <memory>
#include <string_view>
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

        /// Stands the next free instance at `footPosition`, facing `yaw`, playing `clip`.
        /// False when the pool is exhausted, which only happens if the level frees more
        /// robots than the index counted.
        bool Place(const glm::vec3& footPosition, float yaw, std::string_view clip);
        void Update(float deltaSeconds);

        int PlacedCount() const { return placed_; }
        size_t Size() const { return instances_.size(); }

    private:
        struct FInstance
        {
            std::shared_ptr<Assets::Node> worldNode;
            std::vector<Assets::Node*> boneNodes;
            NextGameplay::FRigAnimator animator;
            bool active = false;
        };

        const Assets::FRigAsset* asset_ = nullptr;
        std::vector<FInstance> instances_;
        int placed_ = 0;
    };
}
