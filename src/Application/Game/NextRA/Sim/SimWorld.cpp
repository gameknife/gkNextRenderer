#include "Sim/SimWorld.h"

#include "NextRAConfig.hpp"

namespace NextRA::Sim
{
    namespace
    {
        constexpr FActorId invalidActor = static_cast<FActorId>(-1);

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
        const NextRA::FUnitDef& def = NextRA::UnitDef(typeId);
        const entt::entity entity = registry_.create();
        const FActorId actor = ToActorId(entity);
        actors_.push_back(actor);

        registry_.emplace<FSimTransform>(entity, FSimTransform{pos, pos, WAngle{}, WAngle{}});
        registry_.emplace<FOwner>(entity, FOwner{playerId});
        registry_.emplace<FUnitType>(entity, FUnitType{typeId});
        registry_.emplace<FHealth>(entity, FHealth{def.maxHp, def.maxHp});
        if (def.damage > 0 && def.attackRange.raw > 0)
        {
            registry_.emplace<FAttack>(
                entity,
                FAttack{def.attackRange, def.acquireRange, def.damage, def.cooldownTicks, 0});
        }
        if (def.hasTurret)
        {
            registry_.emplace<FTurret>(entity, FTurret{WAngle{}, WAngle{}, def.turretTurnSpeed, invalidActor});
        }
        auto& mobile = registry_.emplace<FMobile>(entity);
        mobile.speedPerTick = speedPerTick.raw > 0 ? speedPerTick : def.speedPerTick;
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
        const NextRA::FUnitDef& def = NextRA::UnitDef(typeId);
        const entt::entity entity = registry_.create();
        const FActorId actor = ToActorId(entity);
        actors_.push_back(actor);

        registry_.emplace<FSimTransform>(entity, FSimTransform{pos, pos, WAngle{}, WAngle{}});
        registry_.emplace<FOwner>(entity, FOwner{playerId});
        registry_.emplace<FUnitType>(entity, FUnitType{typeId});
        const int32_t maxHp = hp > 0 ? hp : def.maxHp;
        registry_.emplace<FHealth>(entity, FHealth{maxHp, maxHp});
        registry_.emplace<FBuildingTag>(entity);
        auto& footprint = registry_.emplace<FFootprint>(entity);
        footprint.cells = FootprintCellsAt(pos, def.footprint);
        BlockFootprint(footprint, true);
        if (def.damage > 0 && def.attackRange.raw > 0)
        {
            registry_.emplace<FAttack>(
                entity,
                FAttack{def.attackRange, def.acquireRange, def.damage, def.cooldownTicks, 0});
        }
        if (def.hasTurret)
        {
            registry_.emplace<FTurret>(entity, FTurret{WAngle{}, WAngle{}, def.turretTurnSpeed, invalidActor});
        }
        if (isBase || def.base)
        {
            registry_.emplace<FBaseTag>(entity);
        }
        if (hasProduction || def.production)
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
        TurretSystem();
        CombatSystem();
        DeathSystem();
        RebuildOccupancy();
        MovementSystem();
        SeparationSystem();
        TargetingSystem();
        TurretSystem();
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

    const FTurret* FSimWorld::TryGetTurret(FActorId actor) const
    {
        const entt::entity entity = ToEntity(actor);
        return registry_.valid(entity) ? registry_.try_get<FTurret>(entity) : nullptr;
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

    void FSimWorld::SetRenderLink(FActorId actor, uint32_t renderNodeId, uint32_t turretNodeId)
    {
        const entt::entity entity = ToEntity(actor);
        if (!registry_.valid(entity))
        {
            return;
        }

        registry_.emplace_or_replace<FRenderLink>(entity, FRenderLink{renderNodeId, turretNodeId});
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

        const CPos startCell = transform->pos.ToCell();
        const CPos targetCell = target.ToCell();
        WPos pathGoal = target;
        std::vector<CPos> path = grid.FindPath(startCell, targetCell);
        if (path.empty() && !grid.IsPassable(targetCell))
        {
            for (int32_t radius = 1; radius <= 4 && path.empty(); ++radius)
            {
                for (int32_t dz = -radius; dz <= radius && path.empty(); ++dz)
                {
                    for (int32_t dx = -radius; dx <= radius; ++dx)
                    {
                        if (dx != -radius && dx != radius && dz != -radius && dz != radius)
                        {
                            continue;
                        }

                        const CPos candidate{targetCell.x + dx, targetCell.z + dz};
                        if (!grid.IsPassable(candidate))
                        {
                            continue;
                        }

                        path = grid.FindPath(startCell, candidate);
                        if (!path.empty())
                        {
                            pathGoal = WPos::FromCells(candidate.x, candidate.z);
                            break;
                        }
                    }
                }
            }
        }
        if (path.empty())
        {
            return false;
        }

        SetMovePath(*mobile, pathGoal, std::move(path));
        if (auto* attack = registry_.try_get<FAttack>(entity))
        {
            attack->targetActor = invalidActor;
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
        const NextRA::FUnitDef& produceDef = NextRA::UnitDef(produceTypeId);
        if (!NextRA::IsKnownUnitType(produceTypeId) || !produceDef.mobile || produceDef.building ||
            produceDef.productionBuildTicks <= 0)
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

    void FSimWorld::RebuildOccupancy()
    {
        occupancy_.Clear();
        for (FActorId actor : actors_)
        {
            const entt::entity entity = ToEntity(actor);
            if (!registry_.valid(entity))
            {
                continue;
            }

            const auto* transform = registry_.try_get<FSimTransform>(entity);
            const auto* unitType = registry_.try_get<FUnitType>(entity);
            const auto* health = registry_.try_get<FHealth>(entity);
            if (!transform || !unitType || (health && health->hp <= 0))
            {
                continue;
            }

            for (CPos cell : FootprintCellsAt(transform->pos, NextRA::UnitDef(unitType->typeId).footprint))
            {
                occupancy_.Add(actor, cell);
            }
        }
    }

    std::vector<CPos> FSimWorld::FootprintCellsAt(const WPos& pos, CPos footprint) const
    {
        const int32_t width = footprint.x <= 0 ? 1 : footprint.x;
        const int32_t height = footprint.z <= 0 ? 1 : footprint.z;
        const CPos center = pos.ToCell();
        const int32_t startX = center.x - width / 2;
        const int32_t startZ = center.z - height / 2;

        std::vector<CPos> cells;
        cells.reserve(static_cast<size_t>(width * height));
        for (int32_t dz = 0; dz < height; ++dz)
        {
            for (int32_t dx = 0; dx < width; ++dx)
            {
                cells.push_back(CPos{startX + dx, startZ + dz});
            }
        }
        return cells;
    }

    void FSimWorld::BlockFootprint(const FFootprint& footprint, bool blocked)
    {
        if (!pathGrid_)
        {
            return;
        }

        for (CPos cell : footprint.cells)
        {
            pathGrid_->SetBlocked(cell, blocked);
        }
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
                        NextRA::UnitDef(producedType).speedPerTick,
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

            if (attack->targetActor != invalidActor &&
                IsAliveEnemyTarget(actor, attack->targetActor))
            {
                continue;
            }

            attack->targetActor = invalidActor;
            const bool canAcquire = attack->attackMove || (mobile && mobile->hasGoal) || !mobile;
            if (!canAcquire)
            {
                continue;
            }

            FActorId bestTarget = invalidActor;
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
                    (bestTarget == invalidActor || distance < bestDistance ||
                     (distance == bestDistance && target < bestTarget)))
                {
                    bestDistance = distance;
                    bestTarget = target;
                }
            }

            attack->targetActor = bestTarget;
        }
    }

    void FSimWorld::TurretSystem()
    {
        for (FActorId actor : actors_)
        {
            const entt::entity entity = ToEntity(actor);
            if (!registry_.valid(entity))
            {
                continue;
            }

            auto* turret = registry_.try_get<FTurret>(entity);
            const auto* transform = registry_.try_get<FSimTransform>(entity);
            if (!turret || !transform)
            {
                continue;
            }

            turret->prevFacing = turret->facing;
            turret->targetActor = invalidActor;

            const auto* attack = registry_.try_get<FAttack>(entity);
            if (!attack || attack->targetActor == invalidActor || !IsAliveEnemyTarget(actor, attack->targetActor))
            {
                continue;
            }

            const FSimTransform* targetTransform = TryGetTransform(attack->targetActor);
            if (!targetTransform)
            {
                continue;
            }

            const WVec toTarget = targetTransform->pos - transform->pos;
            turret->targetActor = attack->targetActor;
            turret->facing = TurnToward(turret->facing, Atan2FromVec2(toTarget.x, toTarget.z), turret->turnSpeed);
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

            if (attack->targetActor == invalidActor ||
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

            const FUnitType* attackerType = registry_.try_get<FUnitType>(entity);
            const FUnitType* targetType = TryGetUnitType(attack->targetActor);
            const NextRA::EWeaponType weapon =
                attackerType ? NextRA::UnitDef(attackerType->typeId).weapon : NextRA::EWeaponType::Bullet;
            const NextRA::EArmorType armor =
                targetType ? NextRA::UnitDef(targetType->typeId).armor : NextRA::EArmorType::Flesh;
            targetHealth->hp -= NextRA::ApplyDamageMultiplier(attack->damage, weapon, armor);
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
            if (const auto* footprint = registry_.try_get<FFootprint>(entity))
            {
                BlockFootprint(*footprint, false);
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
                attack && attack->targetActor != invalidActor)
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

            const CPos currentCell = transform->pos.ToCell();
            const CPos stepCell = stepGoal.ToCell();
            if (stepCell != currentCell && occupancy_.IsOccupiedByOther(stepCell, actor))
            {
                continue;
            }

            const WPos previousPos = transform->pos;
            transform->pos = MoveTowards2D(transform->pos, stepGoal, mobile->speedPerTick);
            const WVec moved = transform->pos - previousPos;
            if (!SamePos2D(transform->pos, previousPos))
            {
                const FUnitType* unitType = registry_.try_get<FUnitType>(entity);
                const WAngle turnSpeed =
                    unitType ? NextRA::UnitDef(unitType->typeId).bodyTurnSpeed : WAngle::FromRaw(64);
                transform->facing = TurnToward(transform->facing, Atan2FromVec2(moved.x, moved.z), turnSpeed);
            }
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

    void FSimWorld::SeparationSystem()
    {
        RebuildOccupancy();
        constexpr FFixed minSpacing = FFixed::FromInt(cellSubUnits * 3 / 4);
        constexpr FFixed pushDistance = FFixed::FromInt(cellSubUnits / 8);
        const FFixed minSpacingSq = minSpacing * minSpacing;

        for (FActorId actor : actors_)
        {
            const entt::entity entity = ToEntity(actor);
            if (!registry_.valid(entity))
            {
                continue;
            }

            auto* transform = registry_.try_get<FSimTransform>(entity);
            const auto* mobile = registry_.try_get<FMobile>(entity);
            const auto* health = registry_.try_get<FHealth>(entity);
            if (!transform || !mobile || (health && health->hp <= 0))
            {
                continue;
            }

            const CPos cell = transform->pos.ToCell();
            bool pushed = false;
            for (int32_t dz = -1; dz <= 1 && !pushed; ++dz)
            {
                for (int32_t dx = -1; dx <= 1 && !pushed; ++dx)
                {
                    const CPos neighbor{cell.x + dx, cell.z + dz};
                    for (FActorId other : occupancy_.ActorsAt(neighbor))
                    {
                        if (other >= actor)
                        {
                            continue;
                        }

                        const FSimTransform* otherTransform = TryGetTransform(other);
                        const FMobile* otherMobile = TryGetMobile(other);
                        if (!otherTransform || !otherMobile)
                        {
                            continue;
                        }

                        if (LengthSquared2D(transform->pos - otherTransform->pos) >= minSpacingSq)
                        {
                            continue;
                        }

                        const CPos otherCell = otherTransform->pos.ToCell();
                        const int32_t diffX = cell.x - otherCell.x;
                        const int32_t diffZ = cell.z - otherCell.z;
                        int32_t pushX = 0;
                        int32_t pushZ = 0;
                        if (diffX == 0 && diffZ == 0)
                        {
                            pushX = 1;
                        }
                        else if ((diffX < 0 ? -diffX : diffX) >= (diffZ < 0 ? -diffZ : diffZ))
                        {
                            pushX = diffX < 0 ? -1 : 1;
                        }
                        else
                        {
                            pushZ = diffZ < 0 ? -1 : 1;
                        }

                        WPos nextPos = transform->pos;
                        nextPos.x += pushDistance * FFixed::FromInt(pushX);
                        nextPos.z += pushDistance * FFixed::FromInt(pushZ);
                        if (pathGrid_ && !pathGrid_->IsPassable(nextPos.ToCell()))
                        {
                            continue;
                        }

                        transform->pos = nextPos;
                        pushed = true;
                        break;
                    }
                }
            }
        }
    }
}
