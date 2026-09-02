#pragma once

// ============================================================================
// InteractableSystem.hpp - Everything the player breaks, frees or activates:
// crates, breakable brick walls and chests (punch), cages (punch to raise the
// dome), stranded robots (walk up and hold), checkpoints and the goal arch.
// The mechanism system owns the moving parts; this system owns the rules.
// ============================================================================

#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextAstrobot/Level/LevelIndex.hpp"
#include "Application/Game/NextAstrobot/NextAstrobotConfig.hpp"

namespace Assets
{
    class Node;
}

namespace NextAstrobot
{
    class FMechanismSystem;

    struct FInteractionEvent
    {
        int rescued = 0;
        int smashed = 0;
        int coinsFromSmash = 0;
        int checkpointReached = -1;   // -1 = none this frame
        bool goalReached = false;
        std::string toast;            // short HUD message, empty when nothing happened
    };

    class FInteractableSystem
    {
    public:
        void Configure(const FWorldConfig& config) { config_ = config; }
        void Bind(const FLevelIndex& index, FMechanismSystem& mechanisms);
        void Unbind();

        FInteractionEvent Update(float deltaSeconds, const glm::vec3& playerFoot, bool punchStarted,
                                 const glm::vec3& punchOrigin, const glm::vec3& punchDirection, float punchRange,
                                 float punchArcDegrees);

        int RescueTotal() const { return rescueTotal_; }
        /// Foot position of the active respawn point (the spawn pad until a flag is reached).
        const glm::vec3& RespawnPosition() const { return respawnPosition_; }
        float RespawnYaw() const { return respawnYaw_; }
        int ActiveCheckpoint() const { return activeCheckpoint_; }
        void SetSpawn(const glm::vec3& position, float yaw);

    private:
        struct FBreakable
        {
            Assets::Node* node = nullptr;
            glm::vec3 position{0.0f};
            EInteractableKind kind = EInteractableKind::Crate;
            float radius = 1.0f;
            bool broken = false;
        };
        struct FRescue
        {
            Assets::Node* node = nullptr;
            bool inCage = false;       // freed by punching the cage open, not by walking up
            glm::vec3 position{0.0f};
            float hold = 0.0f;
            bool freed = false;
        };
        struct FCheckpoint
        {
            glm::vec3 position{0.0f};
            int index = 0;
            bool reached = false;
        };

        FWorldConfig config_{};
        FMechanismSystem* mechanisms_ = nullptr;
        std::vector<FBreakable> breakables_;
        std::vector<FRescue> rescues_;
        std::vector<FCheckpoint> checkpoints_;
        glm::vec3 goalPosition_{0.0f};
        bool hasGoal_ = false;
        bool goalReached_ = false;
        int rescueTotal_ = 0;
        glm::vec3 respawnPosition_{0.0f};
        float respawnYaw_ = 0.0f;
        int activeCheckpoint_ = -1;
    };
}
