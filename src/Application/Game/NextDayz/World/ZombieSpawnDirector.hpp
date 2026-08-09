#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include "Application/Game/NextDayz/Data/DeterministicRng.hpp"
#include "Application/Game/NextDayz/Data/ZombieDefs.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieSystem.hpp"

namespace NextDayz
{
    struct FZombieSpawnPoint
    {
        glm::vec3 position{};
        EZombieProfile profile = EZombieProfile::Civilian;
        FZombieHandle activeZombie{};
        double cooldownUntil = 0.0;
        float farCalmSeconds = 0.0f;
    };

    class ZombieSpawnDirector
    {
    public:
        void Reset(uint64_t seed);
        void SetPoints(std::vector<FZombieSpawnPoint> points);
        void Update(float deltaSeconds, const glm::vec3& playerPosition, ZombieSystem& zombies,
                    const std::function<bool(const glm::vec3&)>& isVisible,
                    const std::function<bool(const glm::vec3&)>& isNavigable);

        int SpawnCount() const { return spawnCount_; }
        int RejectedVisible() const { return rejectedVisible_; }
        const std::vector<FZombieSpawnPoint>& Points() const { return points_; }

    private:
        std::vector<FZombieSpawnPoint> points_;
        FDeterministicRng rng_;
        double worldSeconds_ = 0.0;
        float evaluationAccumulator_ = 0.0f;
        int spawnCount_ = 0;
        int rejectedVisible_ = 0;
        int globalCap_ = 24;
    };
}
