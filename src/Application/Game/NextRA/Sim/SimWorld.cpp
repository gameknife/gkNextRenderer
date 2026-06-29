#include "Sim/SimWorld.h"

#include "NextRAConfig.hpp"

namespace NextRA::Sim
{
    namespace
    {
        entt::entity ToEntity(FActorId actor)
        {
            return static_cast<entt::entity>(actor);
        }

        FActorId ToActorId(entt::entity entity)
        {
            return static_cast<FActorId>(entity);
        }
    }

    FActorId FSimWorld::SpawnMobile(uint8_t playerId, uint16_t typeId, const WPos& pos, const WPos& goal,
                                    FFixed speedPerTick, bool pingPong, bool notifySpawn)
    {
        const entt::entity entity = registry_.create();
        const FActorId actor = ToActorId(entity);
        actors_.push_back(actor);

        registry_.emplace<FSimTransform>(entity, FSimTransform{pos, pos, WAngle{}, WAngle{}});
        registry_.emplace<FOwner>(entity, FOwner{playerId});
        registry_.emplace<FUnitType>(entity, FUnitType{typeId});
        const int32_t maxHp = NextRA::UnitMaxHp(typeId);
        registry_.emplace<FHealth>(entity, FHealth{maxHp, maxHp});
        registry_.emplace<FAttack>(
            entity,
            FAttack{NextRA::UnitAttackRange(typeId), CellDistance(5), NextRA::UnitDamage(typeId), 12, 0});
        auto& mobile = registry_.emplace<FMobile>(entity);
        mobile.speedPerTick = speedPerTick;
        mobile.pointA = pos;
        mobile.pointB = goal;
        mobile.goal = goal;
        mobile.hasGoal = true;
        mobile.pingPong = pingPong;

        if (notifySpawn)
        {
            spawnedActorIds_.push_back(actor);
        }
        return actor;
    }

    FActorId FSimWorld::SpawnBuilding(uint8_t playerId, uint16_t typeId, const WPos& pos, int32_t hp,
                                      bool isBase, bool hasProduction, const WPos& rallyPoint)
    {
        const entt::entity entity = registry_.create();
        const FActorId actor = ToActorId(entity);
        actors_.push_back(actor);

        registry_.emplace<FSimTransform>(entity, FSimTransform{pos, pos, WAngle{}, WAngle{}});
        registry_.emplace<FOwner>(entity, FOwner{playerId});
        registry_.emplace<FUnitType>(entity, FUnitType{typeId});
        registry_.emplace<FHealth>(entity, FHealth{hp, hp});
        registry_.emplace<FBuildingTag>(entity);
        if (isBase)
        {
            registry_.emplace<FBaseTag>(entity);
        }
        if (hasProduction)
        {
            registry_.emplace<FProduction>(entity, FProduction{0, 0, rallyPoint});
        }
        return actor;
    }

    void FSimWorld::Step(uint32_t tick)
    {
        currentTick_ = tick;
        ProductionSystem();
        TargetingSystem();
        CombatSystem();
        DeathSystem();
        MovementSystem();
        TargetingSystem();
        CombatSystem();
        DeathSystem();
    }

    bool FSimWorld::IsAlive(FActorId actor) const
    {
        return registry_.valid(ToEntity(actor));
    }

    const FSimTransform* FSimWorld::TryGetTransform(FActorId actor) const
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FSimTransform>(entity) : nullptr;
    }

    FSimTransform* FSimWorld::TryGetTransform(FActorId actor)
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FSimTransform>(entity) : nullptr;
    }

    const FOwner* FSimWorld::TryGetOwner(FActorId actor) const
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FOwner>(entity) : nullptr;
    }

    const FHealth* FSimWorld::TryGetHealth(FActorId actor) const
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FHealth>(entity) : nullptr;
    }

    FHealth* FSimWorld::TryGetHealth(FActorId actor)
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FHealth>(entity) : nullptr;
    }

    const FAttack* FSimWorld::TryGetAttack(FActorId actor) const
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FAttack>(entity) : nullptr;
    }

    const FUnitType* FSimWorld::TryGetUnitType(FActorId actor) const
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FUnitType>(entity) : nullptr;
    }

    const FMobile* FSimWorld::TryGetMobile(FActorId actor) const
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FMobile>(entity) : nullptr;
    }

    const FProduction* FSimWorld::TryGetProduction(FActorId actor) const
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FProduction>(entity) : nullptr;
    }

    bool FSimWorld::IsBase(FActorId actor) const
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) && registry_.all_of<FBaseTag>(entity);
    }

    void FSimWorld::SetRenderLink(FActorId actor, uint32_t renderNodeId)
    {
        const entt::entity entity = ToEntity(actor);
        if (!registry_.valid(entity))
        {
            return;
        }

        registry_.emplace_or_replace<FRenderLink>(entity, FRenderLink{renderNodeId});
    }

    const FRenderLink* FSimWorld::TryGetRenderLink(FActorId actor) const
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FRenderLink>(entity) : nullptr;
    }

    bool FSimWorld::IssueMove(FActorId actor, const WPos& target, const FPathfindGrid& grid)
    {
        const entt::entity entity = ToEntity(actor);
        if (!registry_.valid(entity))
        {
            return false;
        }

        auto* transform = registry_.try_get<FSimTransform>(entity);
        auto* mobile = registry_.try_get<FMobile>(entity);
        if (!transform || !mobile)
        {
            return false;
        }

        std::vector<CPos> path = grid.FindPath(transform->pos.ToCell(), target.ToCell());
        if (path.empty())
        {
            return false;
        }

        SetMovePath(*mobile, target, std::move(path));
        if (auto* attack = registry_.try_get<FAttack>(entity))
        {
            attack->targetActor = static_cast<FActorId>(-1);
            attack->attackMove = false;
        }
        return true;
    }

    bool FSimWorld::IssueAttackMove(FActorId actor, const WPos& target, const FPathfindGrid& grid)
    {
        if (!IssueMove(actor, target, grid))
        {
            return false;
        }

        const entt::entity entity = ToEntity(actor);
        if (auto* attack = registry_.try_get<FAttack>(entity))
        {
            attack->attackMove = true;
        }
        return true;
    }

    bool FSimWorld::IssueAttack(FActorId actor, FActorId targetActor)
    {
        const entt::entity entity = ToEntity(actor);
        if (!registry_.valid(entity) || !IsAliveEnemyTarget(actor, targetActor))
        {
            return false;
        }

        auto* attack = registry_.try_get<FAttack>(entity);
        if (!attack)
        {
            return false;
        }

        attack->targetActor = targetActor;
        attack->attackMove = false;
        return true;
    }

    bool FSimWorld::IssueProduce(FActorId actor, uint16_t produceTypeId)
    {
        if (produceTypeId != NextRA::infantryTypeId && produceTypeId != NextRA::tankTypeId)
        {
            return false;
        }

        const entt::entity entity = ToEntity(actor);
        if (!registry_.valid(entity))
        {
            return false;
        }

        auto* production = registry_.try_get<FProduction>(entity);
        if (!production || production->queuedTypeId != 0)
        {
            return false;
        }

        production->queuedTypeId = produceTypeId;
        production->progressLeft = NextRA::ProductionBuildTicks(produceTypeId);
        return true;
    }

    std::vector<uint32_t> FSimWorld::ConsumeDestroyedRenderNodeIds()
    {
        std::vector<uint32_t> ids = std::move(destroyedRenderNodeIds_);
        destroyedRenderNodeIds_.clear();
        return ids;
    }

    std::vector<FActorId> FSimWorld::ConsumeSpawnedActorIds()
    {
        std::vector<FActorId> ids = std::move(spawnedActorIds_);
        spawnedActorIds_.clear();
        return ids;
    }

    void FSimWorld::SetMovePath(FMobile& mobile, const WPos& target, std::vector<CPos> path)
    {
        mobile.goal = target;
        mobile.path = std::move(path);
        mobile.pathCursor = mobile.path.size() > 1 ? 1u : 0u;
        mobile.hasGoal = true;
        mobile.pingPong = false;
    }

    bool FSimWorld::IsEnemy(FActorId lhs, FActorId rhs) const
    {
        const FOwner* lhsOwner = TryGetOwner(lhs);
        const FOwner* rhsOwner = TryGetOwner(rhs);
        return lhsOwner && rhsOwner && lhsOwner->playerId != rhsOwner->playerId;
    }

    bool FSimWorld::IsAliveEnemyTarget(FActorId actor, FActorId target) const
    {
        const FHealth* health = TryGetHealth(target);
        return IsAlive(target) && health && health->hp > 0 && IsEnemy(actor, target);
    }

    void FSimWorld::ProductionSystem()
    {
        const size_t actorCount = actors_.size();
        for (size_t index = 0; index < actorCount; ++index)
        {
            const entt::entity entity = ToEntity(actors_[index]);
            if (!registry_.valid(entity))
            {
                continue;
            }

            auto* production = registry_.try_get<FProduction>(entity);
            const auto* owner = registry_.try_get<FOwner>(entity);
            if (!production || !owner || production->queuedTypeId == 0)
            {
                continue;
            }

            if (production->progressLeft > 0)
            {
                --production->progressLeft;
            }
            if (production->progressLeft > 0)
            {
                continue;
            }

            const uint16_t producedType = production->queuedTypeId;
            production->queuedTypeId = 0;
            production->progressLeft = 0;
            SpawnMobile(owner->playerId,
                        producedType,
                        production->rallyPoint,
                        production->rallyPoint,
                        NextRA::UnitSpeedPerTick(producedType),
                        false,
                        true);
        }
    }

    void FSimWorld::TargetingSystem()
    {
        for (FActorId actor : actors_)
        {
            const entt::entity entity = ToEntity(actor);
            if (!registry_.valid(entity))
            {
                continue;
            }

            auto* attack = registry_.try_get<FAttack>(entity);
            const auto* transform = registry_.try_get<FSimTransform>(entity);
            const auto* mobile = registry_.try_get<FMobile>(entity);
            if (!attack || !transform)
            {
                continue;
            }

            if (attack->targetActor != static_cast<FActorId>(-1) &&
                IsAliveEnemyTarget(actor, attack->targetActor))
            {
                continue;
            }

            attack->targetActor = static_cast<FActorId>(-1);
            const bool canAcquire = attack->attackMove || (mobile && mobile->hasGoal) || !mobile;
            if (!canAcquire)
            {
                continue;
            }

            FActorId bestTarget = static_cast<FActorId>(-1);
            FFixed bestDistance = FFixed::FromInt(999999);
            for (FActorId target : actors_)
            {
                if (!IsAliveEnemyTarget(actor, target))
                {
                    continue;
                }

                const FSimTransform* targetTransform = TryGetTransform(target);
                if (!targetTransform)
                {
                    continue;
                }

                const FFixed distance = Length2D(targetTransform->pos - transform->pos);
                if (distance <= attack->acquireRange &&
                    (bestTarget == static_cast<FActorId>(-1) || distance < bestDistance ||
                     (distance == bestDistance && target < bestTarget)))
                {
                    bestDistance = distance;
                    bestTarget = target;
                }
            }

            attack->targetActor = bestTarget;
        }
    }

    void FSimWorld::CombatSystem()
    {
        for (FActorId actor : actors_)
        {
            const entt::entity entity = ToEntity(actor);
            if (!registry_.valid(entity))
            {
                continue;
            }

            auto* attack = registry_.try_get<FAttack>(entity);
            const auto* transform = registry_.try_get<FSimTransform>(entity);
            if (!attack || !transform)
            {
                continue;
            }

            if (attack->cooldownLeft > 0)
            {
                --attack->cooldownLeft;
            }

            if (attack->targetActor == static_cast<FActorId>(-1) ||
                !IsAliveEnemyTarget(actor, attack->targetActor))
            {
                continue;
            }

            const FSimTransform* targetTransform = TryGetTransform(attack->targetActor);
            FHealth* targetHealth = TryGetHealth(attack->targetActor);
            if (!targetTransform || !targetHealth)
            {
                continue;
            }

            if (Length2D(targetTransform->pos - transform->pos) > attack->range || attack->cooldownLeft > 0)
            {
                continue;
            }

            targetHealth->hp -= attack->damage;
            attack->cooldownLeft = attack->cooldownTicks;
        }
    }

    void FSimWorld::DeathSystem()
    {
        for (FActorId actor : actors_)
        {
            const entt::entity entity = ToEntity(actor);
            if (!registry_.valid(entity))
            {
                continue;
            }

            const auto* health = registry_.try_get<FHealth>(entity);
            if (!health || health->hp > 0)
            {
                continue;
            }

            if (const auto* renderLink = registry_.try_get<FRenderLink>(entity))
            {
                destroyedRenderNodeIds_.push_back(renderLink->renderNodeId);
            }
            if (registry_.all_of<FBaseTag>(entity) && winnerPlayerId_ < 0)
            {
                const auto* owner = registry_.try_get<FOwner>(entity);
                if (owner)
                {
                    winnerPlayerId_ = owner->playerId == 0 ? 1 : 0;
                }
            }
            registry_.destroy(entity);
        }
    }

    void FSimWorld::MovementSystem()
    {
        for (FActorId actor : actors_)
        {
            const entt::entity entity = ToEntity(actor);
            if (!registry_.valid(entity))
            {
                continue;
            }

            auto* transform = registry_.try_get<FSimTransform>(entity);
            auto* mobile = registry_.try_get<FMobile>(entity);
            if (!transform || !mobile)
            {
                continue;
            }

            transform->prevPos = transform->pos;
            transform->prevFacing = transform->facing;

            if (!mobile->hasGoal)
            {
                continue;
            }

            if (const auto* attack = registry_.try_get<FAttack>(entity);
                attack && attack->targetActor != static_cast<FActorId>(-1))
            {
                const FSimTransform* targetTransform = TryGetTransform(attack->targetActor);
                if (targetTransform && Length2D(targetTransform->pos - transform->pos) <= attack->range)
                {
                    continue;
                }
            }

            WPos stepGoal = mobile->goal;
            if (!mobile->path.empty() && mobile->pathCursor < mobile->path.size())
            {
                stepGoal = WPos::FromCells(mobile->path[mobile->pathCursor].x, mobile->path[mobile->pathCursor].z);
            }

            transform->pos = MoveTowards2D(transform->pos, stepGoal, mobile->speedPerTick);
            if (!SamePos2D(transform->pos, stepGoal))
            {
                continue;
            }

            if (!mobile->path.empty() && mobile->pathCursor + 1 < mobile->path.size())
            {
                ++mobile->pathCursor;
                continue;
            }

            transform->pos = mobile->goal;
            if (SamePos2D(transform->pos, mobile->goal))
            {
                if (mobile->pingPong)
                {
                    mobile->goal = SamePos2D(mobile->goal, mobile->pointB) ? mobile->pointA : mobile->pointB;
                    mobile->path.clear();
                    mobile->pathCursor = 0;
                }
                else
                {
                    mobile->hasGoal = false;
                    mobile->path.clear();
                    mobile->pathCursor = 0;
                }
            }
        }
    }
}
