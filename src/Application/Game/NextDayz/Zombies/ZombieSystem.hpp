#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include <span>

#include "Application/Game/NextDayz/Combat/CombatEvents.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/Data/ZombieDefs.hpp"

namespace NextDayz
{
    enum class EZombieState : uint8_t
    {
        Dormant,
        Wander,
        Investigate,
        Chase,
        Attack,
        Stagger,
        Dead,
    };

    struct FZombieRuntime
    {
        FZombieHandle handle{};
        EZombieProfile profile = EZombieProfile::Civilian;
        EZombieState state = EZombieState::Dormant;
        glm::vec3 position{};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
        glm::vec3 spawnPosition{};
        glm::vec3 lastKnownPlayerPosition{};
        float health = 100.0f;
        float stateSeconds = 0.0f;
        float attackTimer = 0.0f;
        float attackCooldown = 0.0f;
        float lostSightSeconds = 0.0f;
        float corpseSeconds = 0.0f;
        float repathSeconds = 0.0f;
        float stuckSeconds = 0.0f;
        glm::vec3 pathTarget{};
        glm::vec3 progressPosition{};
        std::vector<glm::vec3> path;
        size_t pathWaypoint = 0;
        uint64_t lastNoiseSequence = 0;
        bool attackCommitted = false;
        bool active = false;
    };

    struct FPlayerDamageRequest
    {
        FZombieHandle source{};
        float amount = 0.0f;
    };

    class ZombieSystem
    {
    public:
        using FPathResolver = std::function<std::vector<glm::vec3>(const glm::vec3&, const glm::vec3&)>;

        explicit ZombieSystem(size_t capacity = 32);

        void Reset();
        FZombieHandle Spawn(EZombieProfile profile, const glm::vec3& position);
        bool Recycle(FZombieHandle handle);
        bool ApplyDamage(FZombieHandle handle, float damage, EHitZone hitZone);

        void Update(float deltaSeconds, const glm::vec3& playerPosition, const glm::vec3& playerForward,
                    bool playerAlive, const std::function<bool(const glm::vec3&, const glm::vec3&)>& hasLineOfSight,
                    std::span<const FNoiseEvent> noises, const FPathResolver& resolvePath = {});

        const FZombieRuntime* Resolve(FZombieHandle handle) const;
        FZombieRuntime* Resolve(FZombieHandle handle);
        std::vector<FPlayerDamageRequest> ConsumePlayerDamageRequests();

        int ActiveCount() const;
        int AlertedCount() const;
        int KillCount() const { return killCount_; }
        size_t Capacity() const { return slots_.size(); }
        const std::vector<FZombieRuntime>& Slots() const { return slots_; }

    private:
        static void MoveToward(FZombieRuntime& zombie, const glm::vec3& target, float speed, float deltaSeconds);
        static bool NavigateToward(FZombieRuntime& zombie, const glm::vec3& target, float speed,
                                   float deltaSeconds, const FPathResolver& resolvePath);
        bool CanSeePlayer(const FZombieRuntime& zombie, const glm::vec3& playerPosition,
                          const std::function<bool(const glm::vec3&, const glm::vec3&)>& hasLineOfSight) const;

        std::vector<FZombieRuntime> slots_;
        std::vector<uint32_t> generations_;
        std::vector<FPlayerDamageRequest> damageRequests_;
        int killCount_ = 0;
    };
}
