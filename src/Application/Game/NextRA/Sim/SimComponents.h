#pragma once

#include "Sim/WMath.h"

#include <cstdint>
#include <vector>

namespace NextRA::Sim
{
    using FActorId = uint32_t;

    struct FSimTransform
    {
        WPos prevPos;
        WPos pos;
        WAngle prevFacing;
        WAngle facing;
    };

    struct FOwner
    {
        uint8_t playerId = 0;
    };

    struct FHealth
    {
        int32_t hp = 1;
        int32_t maxHp = 1;
    };

    struct FMobile
    {
        FFixed speedPerTick = FFixed::FromInt(0);
        WPos pointA;
        WPos pointB;
        WPos goal;
        std::vector<CPos> path;
        size_t pathCursor = 0;
        bool hasGoal = false;
        bool pingPong = false;
    };

    struct FBuildingTag
    {
    };

    struct FBaseTag
    {
    };

    struct FProduction
    {
        uint16_t queuedTypeId = 0;
        int32_t progressLeft = 0;
        WPos rallyPoint;
    };

    struct FAttack
    {
        WDist range = CellDistance(2);
        WDist acquireRange = CellDistance(5);
        int32_t damage = 20;
        int32_t cooldownTicks = 12;
        int32_t cooldownLeft = 0;
        FActorId targetActor = static_cast<FActorId>(-1);
        bool attackMove = false;
    };

    struct FUnitType
    {
        uint16_t typeId = 0;
    };

    struct FRenderLink
    {
        uint32_t renderNodeId = 0;
    };
}
