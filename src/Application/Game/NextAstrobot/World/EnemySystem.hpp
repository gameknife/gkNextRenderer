#pragma once

// ============================================================================
// EnemySystem.hpp - Walkers and spiky-backs patrol a fixed segment through their
// authored placement; flyers hover in a circle. Enemies own no collider, so
// combat is geometric: landing on top of one kills it and bounces the player,
// touching one from the side kills the player, and a punch knocks one back (and
// kills anything but a spiky back).
// ============================================================================

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
    struct FEnemyOutcome
    {
        bool stomped = false;   // player killed an enemy by landing on it
        bool killedPlayer = false;
        int punchedCount = 0;
    };

    class FEnemySystem
    {
    public:
        void Configure(const FWorldConfig& config) { config_ = config; }
        void Bind(const FLevelIndex& index);
        void Unbind();
        /// Puts every enemy back on its patrol, alive; used when the player respawns.
        void ResetAll();

        FEnemyOutcome Update(float time, float deltaSeconds, const glm::vec3& playerFoot, float playerHeight,
                             float playerFallSpeed, bool punchActive, const glm::vec3& punchOrigin,
                             const glm::vec3& punchDirection, float punchRange, float punchArcDegrees);

        int AliveCount() const;
        size_t Count() const { return enemies_.size(); }

    private:
        struct FEnemy
        {
            Assets::Node* node = nullptr;
            EEnemyKind kind = EEnemyKind::Walker;
            glm::vec3 origin{0.0f};
            glm::quat bindRotation{1.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 bindTranslation{0.0f};
            glm::vec3 position{0.0f};
            float height = 1.4f;
            float phaseOffset = 0.0f;
            bool alive = true;
            float knockback = 0.0f;
        };

        FWorldConfig config_{};
        std::vector<FEnemy> enemies_;
    };
}
