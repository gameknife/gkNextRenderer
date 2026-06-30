#include "Net/Order.h"
#include "Net/BinaryCodec.h"

namespace NextRA::Net
{
    std::vector<uint8_t> SerializeOrder(const FOrder& order)
    {
        std::vector<uint8_t> out;
        out.reserve(32 + order.actorIds.size() * sizeof(Sim::FActorId));

        WriteBinary<uint8_t>(out, static_cast<uint8_t>(order.type));
        WriteBinary<uint8_t>(out, order.playerId);
        WriteBinary<uint32_t>(out, order.issueTick);
        WriteBinary<uint32_t>(out, static_cast<uint32_t>(order.actorIds.size()));
        for (Sim::FActorId actor : order.actorIds)
        {
            WriteBinary<Sim::FActorId>(out, actor);
        }
        WriteBinary<int64_t>(out, order.targetPos.x.raw);
        WriteBinary<int64_t>(out, order.targetPos.y.raw);
        WriteBinary<int64_t>(out, order.targetPos.z.raw);
        WriteBinary<Sim::FActorId>(out, order.targetActor);
        WriteBinary<uint16_t>(out, order.produceTypeId);
        return out;
    }

    std::optional<FOrder> DeserializeOrder(std::span<const uint8_t> bytes)
    {
        size_t cursor = 0;
        uint8_t type = 0;
        uint8_t playerId = 0;
        uint32_t issueTick = 0;
        uint32_t actorCount = 0;

        if (!ReadBinary(bytes, cursor, type) || !ReadBinary(bytes, cursor, playerId) ||
            !ReadBinary(bytes, cursor, issueTick) || !ReadBinary(bytes, cursor, actorCount))
        {
            return std::nullopt;
        }
        if (type > static_cast<uint8_t>(EOrderType::Produce) || actorCount > 1024)
        {
            return std::nullopt;
        }

        FOrder order;
        order.type = static_cast<EOrderType>(type);
        order.playerId = playerId;
        order.issueTick = issueTick;
        order.actorIds.resize(actorCount);
        for (Sim::FActorId& actor : order.actorIds)
        {
            if (!ReadBinary(bytes, cursor, actor))
            {
                return std::nullopt;
            }
        }

        int64_t x = 0;
        int64_t y = 0;
        int64_t z = 0;
        if (!ReadBinary(bytes, cursor, x) || !ReadBinary(bytes, cursor, y) || !ReadBinary(bytes, cursor, z) ||
            !ReadBinary(bytes, cursor, order.targetActor) || !ReadBinary(bytes, cursor, order.produceTypeId))
        {
            return std::nullopt;
        }
        if (cursor != bytes.size())
        {
            return std::nullopt;
        }

        order.targetPos = Sim::WPos{Sim::FFixed::FromRaw(x), Sim::FFixed::FromRaw(y), Sim::FFixed::FromRaw(z)};
        return order;
    }
}
