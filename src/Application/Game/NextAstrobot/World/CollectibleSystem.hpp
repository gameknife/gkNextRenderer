#pragma once

// ============================================================================
// CollectibleSystem.hpp - Coins, puzzle pieces, gems, keys and the goal star.
// Each one is a scene node from the level: it spins in place, and picking it up
// just hides it. Nothing is pooled or respawned, so the totals in the HUD are
// exactly what the level author placed.
// ============================================================================

#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextAstrobot/Level/LevelIndex.hpp"
#include "Application/Game/NextAstrobot/NextAstrobotConfig.hpp"

namespace Assets
{
    class Node;
    class Scene;
}

namespace NextAstrobot
{
    struct FPickupEvent
    {
        int coins = 0;
        int puzzles = 0;
        int gems = 0;
        int keys = 0;
        bool star = false;
    };

    class FCollectibleSystem
    {
    public:
        void Configure(const FWorldConfig& config) { config_ = config; }
        void Bind(const Assets::Scene& scene, const FLevelIndex& index);
        void Unbind();

        /// Spins the remaining pickups and collects anything within reach of the player.
        FPickupEvent Update(float time, float deltaSeconds, const glm::vec3& playerFoot);

        int CoinsTotal() const { return coinsTotal_; }
        int PuzzlesTotal() const { return puzzlesTotal_; }
        int Remaining() const;

    private:
        struct FItem
        {
            Assets::Node* node = nullptr;
            glm::vec3 position{0.0f};
            glm::quat bindRotation{1.0f, 0.0f, 0.0f, 0.0f};
            ECollectibleKind kind = ECollectibleKind::Coin;
            bool taken = false;
            bool spins = true;
        };

        FWorldConfig config_{};
        std::vector<FItem> items_;
        int coinsTotal_ = 0;
        int puzzlesTotal_ = 0;
    };
}
