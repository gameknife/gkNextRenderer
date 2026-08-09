#pragma once

#include "Sim/OccupancyGrid.h"
#include "Sim/PathfindGrid.h"
#include "Sim/SimRandom.h"
#include "Sim/SimComponents.h"

#include <entt/entt.hpp>
#include <span>

namespace NextRA::Sim
{
    class FSimWorld
    {
    public:
        FActorId SpawnMobile(uint8_t playerId, uint16_t typeId, const WPos& pos, const WPos& goal,
                             FFixed speedPerTick, bool pingPong = false, bool notifySpawn = false);
        FActorId SpawnBuilding(uint8_t playerId, uint16_t typeId, const WPos& pos, int32_t hp,
                               bool isBase, bool hasProduction, const WPos& rallyPoint);

        void Step(uint32_t tick);
        void SetPathGrid(FPathfindGrid* grid) { pathGrid_ = grid; }
        uint32_t CurrentTick() const { return currentTick_; }
        int WinnerPlayerId() const { return winnerPlayerId_; }
        uint64_t RandomState() const { return random_.State(); }
        void SetRandomSeed(uint64_t seed) { random_.SetState(seed); }

        const std::vector<FActorId>& Actors() const { return actors_; }
        bool IsAlive(FActorId actor) const;

        const FSimTransform* TryGetTransform(FActorId actor) const;
        FSimTransform* TryGetTransform(FActorId actor);
        const FOwner* TryGetOwner(FActorId actor) const;
        const FHealth* TryGetHealth(FActorId actor) const;
        FHealth* TryGetHealth(FActorId actor);
        const FAttack* TryGetAttack(FActorId actor) const;
        const FTurret* TryGetTurret(FActorId actor) const;
        const FUnitType* TryGetUnitType(FActorId actor) const;
        const FMobile* TryGetMobile(FActorId actor) const;
        const FProduction* TryGetProduction(FActorId actor) const;
        bool IsBase(FActorId actor) const;
        void SetRenderLink(FActorId actor, uint32_t renderNodeId, uint32_t turretNodeId = 0);
        const FRenderLink* TryGetRenderLink(FActorId actor) const;
        bool IssueMove(FActorId actor, const WPos& target, const FPathfindGrid& grid);
        bool IssueAttackMove(FActorId actor, const WPos& target, const FPathfindGrid& grid);
        bool IssueAttack(FActorId actor, FActorId targetActor);
        bool IssueProduce(FActorId actor, uint16_t produceTypeId);
        std::vector<uint32_t> ConsumeDestroyedRenderNodeIds();
        std::vector<FActorId> ConsumeSpawnedActorIds();
        entt::registry& Registry() { return registry_; }
        const entt::registry& Registry() const { return registry_; }

    private:
        void ProductionSystem();
        void MovementSystem();
        void TurretSystem();
        void SeparationSystem();
        void TargetingSystem();
        void CombatSystem();
        void DeathSystem();
        void RebuildOccupancy();
        std::vector<CPos> FootprintCellsAt(const WPos& pos, CPos footprint) const;
        void BlockFootprint(const FFootprint& footprint, bool blocked);
        void SetMovePath(FMobile& mobile, const WPos& target, std::vector<CPos> path);
        bool IsEnemy(FActorId lhs, FActorId rhs) const;
        bool IsAliveEnemyTarget(FActorId actor, FActorId target) const;

        entt::registry registry_;
        std::vector<FActorId> actors_;
        std::vector<uint32_t> destroyedRenderNodeIds_;
        std::vector<FActorId> spawnedActorIds_;
        FPathfindGrid* pathGrid_ = nullptr;
        FOccupancyGrid occupancy_;
        FSimRandom random_;
        uint32_t currentTick_ = 0;
        int winnerPlayerId_ = -1;
    };
}
