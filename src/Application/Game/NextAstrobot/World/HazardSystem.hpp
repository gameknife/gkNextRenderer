#pragma once

// ============================================================================
// HazardSystem.hpp - Lava, water, spike strips, laser beams, swinging spike
// balls and the kill plane. Hazards have no collider (they are on the
// no-raycast list), so contact is a geometric overlap test against the module's
// own footprint, evaluated in the module's local frame. The two that move are
// read off their live ab_part_* node every frame rather than from a bind pose:
// the spike ball is wherever MechanismSystem swung it to, and the laser is only
// lethal while its beam node is visible.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Application/Game/NextAstrobot/Level/LevelIndex.hpp"
#include "Application/Game/NextAstrobot/NextAstrobotConfig.hpp"

namespace Assets
{
    class Node;
}

namespace NextAstrobot
{
    class FHazardSystem
    {
    public:
        void Configure(const FWorldConfig& config) { config_ = config; }
        void Bind(const FLevelIndex& index, float killPlaneY);
        void Unbind();

        /// Returns a non-empty reason when the player should die this frame.
        std::string Check(const glm::vec3& playerFoot, float playerHeight) const;
        float KillPlaneY() const { return killPlaneY_; }
        size_t Count() const { return hazards_.size(); }

    private:
        struct FHazard
        {
            EHazardKind kind = EHazardKind::Lava;
            /// The module's movable piece, when it has one. Null for a static hazard.
            const Assets::Node* part = nullptr;
            glm::vec3 worldPos{0.0f};
            glm::quat worldRot{1.0f, 0.0f, 0.0f, 0.0f};
            // Interpretation depends on kind: box half-extents, a segment length or a radius.
            glm::vec3 extent{0.0f};
            float radius = 0.0f;
            float surfaceHeight = 0.0f;
        };

        FWorldConfig config_{};
        std::vector<FHazard> hazards_;
        float killPlaneY_ = -1000.0f;
    };
}
