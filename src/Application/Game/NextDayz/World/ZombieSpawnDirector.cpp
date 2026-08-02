#include "Engine/Common/CoreMinimal.hpp"

#include "ZombieSpawnDirector.hpp"

#include <glm/geometric.hpp>

namespace NextDayz
{
    void ZombieSpawnDirector::Reset(uint64_t seed)
    {
        points_.clear();
        rng_.Reset(seed);
        worldSeconds_ = 0.0;
        evaluationAccumulator_ = 0.0f;
        spawnCount_ = 0;
        rejectedVisible_ = 0;
    }

    void ZombieSpawnDirector::SetPoints(std::vector<FZombieSpawnPoint> points)
    {
        points_ = std::move(points);
    }

    void ZombieSpawnDirector::Update(float deltaSeconds, const glm::vec3& playerPosition, ZombieSystem& zombies,
                                     const std::function<bool(const glm::vec3&)>& isVisible,
                                     const std::function<bool(const glm::vec3&)>& isNavigable)
    {
        const float delta = std::max(deltaSeconds, 0.0f);
        worldSeconds_ += delta;
        evaluationAccumulator_ += delta;
        if (evaluationAccumulator_ < 2.0f)
        {
            return;
        }
        evaluationAccumulator_ = std::fmod(evaluationAccumulator_, 2.0f);

        for (FZombieSpawnPoint& point : points_)
        {
            FZombieRuntime* active = point.activeZombie.IsValid() ? zombies.Resolve(point.activeZombie) : nullptr;
            if (point.activeZombie.IsValid() && !active)
            {
                point.activeZombie = {};
                point.cooldownUntil = worldSeconds_ + 8.0 * 60.0 + static_cast<double>(rng_.Range(421));
                point.farCalmSeconds = 0.0f;
                continue;
            }
            if (active)
            {
                const float distance = glm::distance(active->position, playerPosition);
                const bool calm = active->state == EZombieState::Wander || active->state == EZombieState::Dormant;
                point.farCalmSeconds = distance > 180.0f && calm
                    ? point.farCalmSeconds + 2.0f
                    : 0.0f;
                if (point.farCalmSeconds >= 30.0f)
                {
                    zombies.Recycle(point.activeZombie);
                    point.activeZombie = {};
                    point.cooldownUntil = worldSeconds_ + 15.0;
                    point.farCalmSeconds = 0.0f;
                }
            }
        }
        if (zombies.ActiveCount() >= globalCap_ || points_.empty())
        {
            return;
        }

        const size_t start = rng_.Range(static_cast<uint32_t>(points_.size()));
        for (size_t offset = 0; offset < points_.size() && zombies.ActiveCount() < globalCap_; ++offset)
        {
            FZombieSpawnPoint& point = points_[(start + offset) % points_.size()];
            if (point.activeZombie.IsValid() || worldSeconds_ < point.cooldownUntil)
            {
                continue;
            }
            const float distance = glm::distance(point.position, playerPosition);
            if (distance < 35.0f || distance > 140.0f)
            {
                continue;
            }
            if (isVisible && isVisible(point.position))
            {
                ++rejectedVisible_;
                continue;
            }
            if (isNavigable && !isNavigable(point.position))
            {
                continue;
            }
            point.activeZombie = zombies.Spawn(point.profile, point.position);
            if (point.activeZombie.IsValid())
            {
                ++spawnCount_;
            }
        }
    }
}
