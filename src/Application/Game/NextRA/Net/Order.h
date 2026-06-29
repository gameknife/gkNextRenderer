#pragma once

#include "Sim/SimComponents.h"
#include "Sim/WMath.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace NextRA::Net
{
    enum class EOrderType : uint8_t
    {
        Move = 0,
        AttackMove = 1,
        Attack = 2,
        Produce = 3,
    };

    struct FOrder
    {
        EOrderType type = EOrderType::Move;
        uint8_t playerId = 0;
        uint32_t issueTick = 0;
        std::vector<Sim::FActorId> actorIds;
        Sim::WPos targetPos;
        Sim::FActorId targetActor = static_cast<Sim::FActorId>(-1);
        uint16_t produceTypeId = 0;
    };

    std::vector<uint8_t> SerializeOrder(const FOrder& order);
    std::optional<FOrder> DeserializeOrder(std::span<const uint8_t> bytes);
}
