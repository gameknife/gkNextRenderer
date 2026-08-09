#include "Engine/Common/CoreMinimal.hpp"

#include "ZombieSystem.hpp"

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace NextDayz
{
    ZombieSystem::ZombieSystem(size_t capacity)
        : slots_(capacity), generations_(capacity, 1)
    {
        damageRequests_.reserve(capacity);
    }

    void ZombieSystem::Reset()
    {
        for (size_t index = 0; index < slots_.size(); ++index)
        {
            slots_[index] = {};
            ++generations_[index];
            if (generations_[index] == 0)
            {
                ++generations_[index];
            }
        }
        damageRequests_.clear();
        killCount_ = 0;
    }

    FZombieHandle ZombieSystem::Spawn(EZombieProfile profile, const glm::vec3& position)
    {
        for (size_t index = 0; index < slots_.size(); ++index)
        {
            FZombieRuntime& zombie = slots_[index];
            if (zombie.active)
            {
                continue;
            }
            ++generations_[index];
            if (generations_[index] == 0)
            {
                ++generations_[index];
            }
            const FZombieHandle handle{static_cast<uint32_t>(index), generations_[index]};
            zombie = {};
            zombie.handle = handle;
            zombie.profile = profile;
            zombie.state = EZombieState::Wander;
            zombie.position = position;
            zombie.spawnPosition = position;
            zombie.lastKnownPlayerPosition = position;
            zombie.pathTarget = position;
            zombie.progressPosition = position;
            zombie.health = ZombieDef(profile).maxHealth;
            zombie.active = true;
            return handle;
        }
        return {};
    }

    const FZombieRuntime* ZombieSystem::Resolve(FZombieHandle handle) const
    {
        if (!handle.IsValid() || handle.index >= slots_.size() || generations_[handle.index] != handle.generation)
        {
            return nullptr;
        }
        const FZombieRuntime& zombie = slots_[handle.index];
        return zombie.active ? &zombie : nullptr;
    }

    FZombieRuntime* ZombieSystem::Resolve(FZombieHandle handle)
    {
        return const_cast<FZombieRuntime*>(std::as_const(*this).Resolve(handle));
    }

    bool ZombieSystem::Recycle(FZombieHandle handle)
    {
        FZombieRuntime* zombie = Resolve(handle);
        if (!zombie)
        {
            return false;
        }
        zombie->active = false;
        ++generations_[handle.index];
        if (generations_[handle.index] == 0)
        {
            ++generations_[handle.index];
        }
        return true;
    }

    bool ZombieSystem::ApplyDamage(FZombieHandle handle, float damage, EHitZone hitZone)
    {
        FZombieRuntime* zombie = Resolve(handle);
        if (!zombie || zombie->state == EZombieState::Dead || damage <= 0.0f)
        {
            return false;
        }
        const float multiplier = hitZone == EHitZone::Head ? 2.0f : hitZone == EHitZone::Limb ? 0.65f : 1.0f;
        zombie->health = std::max(0.0f, zombie->health - damage * multiplier);
        zombie->stateSeconds = 0.0f;
        if (zombie->health <= 0.0f)
        {
            zombie->state = EZombieState::Dead;
            zombie->corpseSeconds = 0.0f;
            ++killCount_;
        }
        else
        {
            zombie->state = EZombieState::Stagger;
        }
        return true;
    }

    bool ZombieSystem::CanSeePlayer(
        const FZombieRuntime& zombie, const glm::vec3& playerPosition,
        const std::function<bool(const glm::vec3&, const glm::vec3&)>& hasLineOfSight) const
    {
        const FZombieDef& definition = ZombieDef(zombie.profile);
        glm::vec3 delta = playerPosition - zombie.position;
        delta.y = 0.0f;
        const float distance = glm::length(delta);
        if (distance <= 0.01f || distance > definition.sightDistance)
        {
            return false;
        }
        const glm::vec3 direction = delta / distance;
        const float minimumDot = std::cos(glm::radians(definition.fieldOfViewDegrees * 0.5f));
        if (glm::dot(zombie.forward, direction) < minimumDot)
        {
            return false;
        }
        return !hasLineOfSight || hasLineOfSight(zombie.position + glm::vec3(0.0f, 1.4f, 0.0f),
                                                 playerPosition + glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void ZombieSystem::MoveToward(FZombieRuntime& zombie, const glm::vec3& target, float speed, float deltaSeconds)
    {
        glm::vec3 delta = target - zombie.position;
        delta.y = 0.0f;
        const float distance = glm::length(delta);
        if (distance <= 0.01f)
        {
            return;
        }
        zombie.forward = delta / distance;
        zombie.position += zombie.forward * std::min(distance, speed * deltaSeconds);
    }

    bool ZombieSystem::NavigateToward(FZombieRuntime& zombie, const glm::vec3& target, float speed,
                                      float deltaSeconds, const FPathResolver& resolvePath)
    {
        zombie.repathSeconds = std::max(0.0f, zombie.repathSeconds - deltaSeconds);
        glm::vec3 targetDelta = target - zombie.pathTarget;
        targetDelta.y = 0.0f;
        const bool targetMoved = glm::length(targetDelta) >= 2.0f;
        const bool pathFinished = zombie.pathWaypoint >= zombie.path.size();
        if (resolvePath && (zombie.repathSeconds <= 0.0f || targetMoved || pathFinished))
        {
            zombie.path = resolvePath(zombie.position, target);
            zombie.pathWaypoint = zombie.path.size() > 1 ? 1 : 0;
            zombie.pathTarget = target;
            zombie.repathSeconds = 0.6f;
            zombie.stuckSeconds = 0.0f;
            zombie.progressPosition = zombie.position;
            if (zombie.path.empty())
            {
                return false;
            }
        }

        glm::vec3 waypoint = target;
        if (resolvePath)
        {
            while (zombie.pathWaypoint < zombie.path.size() &&
                   glm::distance(zombie.position, zombie.path[zombie.pathWaypoint]) < 0.4f)
            {
                ++zombie.pathWaypoint;
            }
            waypoint = zombie.pathWaypoint < zombie.path.size() ? zombie.path[zombie.pathWaypoint] : target;
        }

        const glm::vec3 before = zombie.position;
        MoveToward(zombie, waypoint, speed, deltaSeconds);
        if (resolvePath && zombie.pathWaypoint < zombie.path.size())
        {
            zombie.position.y = waypoint.y;
        }
        glm::vec3 progress = zombie.position - before;
        progress.y = 0.0f;
        if (glm::length(progress) > 0.01f)
        {
            zombie.stuckSeconds = 0.0f;
            zombie.progressPosition = zombie.position;
        }
        else if (glm::distance(zombie.position, target) > 0.5f)
        {
            zombie.stuckSeconds += deltaSeconds;
            if (zombie.stuckSeconds >= 1.5f)
            {
                // Dynamic blockers are not part of the static grid. Drop the
                // cached route so the next tick re-plans from the current cell.
                zombie.path.clear();
                zombie.pathWaypoint = 0;
                zombie.repathSeconds = 0.0f;
                zombie.stuckSeconds = 0.0f;
            }
        }
        return true;
    }

    void ZombieSystem::Update(
        float deltaSeconds, const glm::vec3& playerPosition, const glm::vec3& /*playerForward*/, bool playerAlive,
        const std::function<bool(const glm::vec3&, const glm::vec3&)>& hasLineOfSight,
        std::span<const FNoiseEvent> noises, const FPathResolver& resolvePath)
    {
        const float delta = std::max(deltaSeconds, 0.0f);
        for (FZombieRuntime& zombie : slots_)
        {
            if (!zombie.active)
            {
                continue;
            }
            zombie.stateSeconds += delta;
            zombie.attackCooldown = std::max(0.0f, zombie.attackCooldown - delta);
            if (zombie.state == EZombieState::Dead)
            {
                zombie.corpseSeconds += delta;
                if (zombie.corpseSeconds >= 25.0f)
                {
                    Recycle(zombie.handle);
                }
                continue;
            }
            if (zombie.state == EZombieState::Stagger)
            {
                if (zombie.stateSeconds >= 0.35f)
                {
                    zombie.state = EZombieState::Chase;
                    zombie.stateSeconds = 0.0f;
                }
                continue;
            }

            const bool seesPlayer = playerAlive && CanSeePlayer(zombie, playerPosition, hasLineOfSight);
            if (seesPlayer)
            {
                zombie.lastKnownPlayerPosition = playerPosition;
                zombie.lostSightSeconds = 0.0f;
                if (zombie.state != EZombieState::Attack)
                {
                    zombie.state = EZombieState::Chase;
                }
            }
            else if (zombie.state == EZombieState::Chase || zombie.state == EZombieState::Attack)
            {
                zombie.lostSightSeconds += delta;
                if (zombie.lostSightSeconds >= 0.6f)
                {
                    zombie.state = EZombieState::Investigate;
                    zombie.stateSeconds = 0.0f;
                }
            }

            float bestNoiseScore = 0.0f;
            for (const FNoiseEvent& noise : noises)
            {
                if (noise.sequence <= zombie.lastNoiseSequence)
                {
                    continue;
                }
                const float distance = glm::distance(noise.position, zombie.position);
                if (distance <= noise.radius)
                {
                    const float score = noise.strength * (1.0f - distance / std::max(noise.radius, 0.01f));
                    if (score > bestNoiseScore)
                    {
                        bestNoiseScore = score;
                        zombie.lastKnownPlayerPosition = noise.position;
                        zombie.lastNoiseSequence = noise.sequence;
                    }
                }
            }
            if (bestNoiseScore > 0.0f && zombie.state != EZombieState::Chase && zombie.state != EZombieState::Attack)
            {
                zombie.state = EZombieState::Investigate;
                zombie.stateSeconds = 0.0f;
            }

            const FZombieDef& definition = ZombieDef(zombie.profile);
            const float playerDistance = glm::distance(zombie.position, playerPosition);
            if (zombie.state == EZombieState::Chase)
            {
                if (playerAlive && playerDistance <= definition.attackRange && zombie.attackCooldown <= 0.0f)
                {
                    zombie.state = EZombieState::Attack;
                    zombie.attackTimer = 0.0f;
                    zombie.attackCommitted = false;
                }
                else
                {
                    if (!NavigateToward(zombie, playerPosition, definition.chaseSpeed, delta, resolvePath))
                    {
                        zombie.state = EZombieState::Investigate;
                        zombie.stateSeconds = 0.0f;
                    }
                }
            }
            else if (zombie.state == EZombieState::Attack)
            {
                zombie.attackTimer += delta;
                if (!zombie.attackCommitted && zombie.attackTimer >= 0.45f)
                {
                    zombie.attackCommitted = true;
                    const glm::vec3 attackDelta = playerPosition - zombie.position;
                    const float commitDistance = glm::length(attackDelta);
                    const bool facingPlayer = commitDistance <= 0.01f ||
                        glm::dot(zombie.forward, attackDelta / commitDistance) > 0.2f;
                    if (playerAlive && commitDistance <= definition.attackRange + 0.15f &&
                        facingPlayer)
                    {
                        damageRequests_.push_back({zombie.handle, definition.attackDamage});
                    }
                }
                if (zombie.attackTimer >= 0.75f)
                {
                    zombie.attackCooldown = 1.1f;
                    zombie.state = EZombieState::Chase;
                    zombie.stateSeconds = 0.0f;
                }
            }
            else if (zombie.state == EZombieState::Investigate)
            {
                if (!NavigateToward(zombie, zombie.lastKnownPlayerPosition,
                                    definition.wanderSpeed * 1.35f, delta, resolvePath))
                {
                    zombie.state = EZombieState::Wander;
                    zombie.stateSeconds = 0.0f;
                }
                if (glm::distance(zombie.position, zombie.lastKnownPlayerPosition) < 0.35f && zombie.stateSeconds > 6.0f)
                {
                    zombie.state = EZombieState::Wander;
                    zombie.stateSeconds = 0.0f;
                }
            }
            else if (zombie.state == EZombieState::Wander && glm::distance(zombie.position, zombie.spawnPosition) > 3.0f)
            {
                NavigateToward(zombie, zombie.spawnPosition, definition.wanderSpeed, delta, resolvePath);
            }
        }
    }

    std::vector<FPlayerDamageRequest> ZombieSystem::ConsumePlayerDamageRequests()
    {
        std::vector<FPlayerDamageRequest> result;
        result.swap(damageRequests_);
        return result;
    }

    int ZombieSystem::ActiveCount() const
    {
        return static_cast<int>(std::count_if(slots_.begin(), slots_.end(),
            [](const FZombieRuntime& zombie) { return zombie.active; }));
    }

    int ZombieSystem::AlertedCount() const
    {
        return static_cast<int>(std::count_if(slots_.begin(), slots_.end(), [](const FZombieRuntime& zombie)
        {
            return zombie.active && (zombie.state == EZombieState::Investigate || zombie.state == EZombieState::Chase ||
                                     zombie.state == EZombieState::Attack);
        }));
    }
}
