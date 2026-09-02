#pragma once

// ============================================================================
// HazardSystem.hpp - Lava, water, spike strips, laser beams, swinging spike
// balls and the kill plane. Hazards have no collider (they are on the
// no-raycast list), so contact is a geometric overlap test against the module's
// own footprint, evaluated in the module's local frame.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextAstrobot/Level/LevelIndex.hpp"
#include "Application/Game/NextAstrobot/NextAstrobotConfig.hpp"

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
