#include "Net/Order.h"

#include <cstring>

namespace NextRA::Net
{
    namespace
    {
        template <typename T>
        void Write(std::vector<uint8_t>& out, T value)
        {
            const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
            out.insert(out.end(), bytes, bytes + sizeof(T));
        }

        template <typename T>
        bool Read(std::span<const uint8_t> bytes, size_t& cursor, T& out)
        {
            if (cursor + sizeof(T) > bytes.size())
            {
                return false;
            }
            std::memcpy(&out, bytes.data() + cursor, sizeof(T));
            cursor += sizeof(T);
            return true;
        }
    }

    std::vector<uint8_t> SerializeOrder(const FOrder& order)
    {
        std::vector<uint8_t> out;
        out.reserve(32 + order.actorIds.size() * sizeof(Sim::FActorId));

        Write<uint8_t>(out, static_cast<uint8_t>(order.type));
        Write<uint8_t>(out, order.playerId);
        Write<uint32_t>(out, order.issueTick);
        Write<uint32_t>(out, static_cast<uint32_t>(order.actorIds.size()));
        for (Sim::FActorId actor : order.actorIds)
        {
            Write<Sim::FActorId>(out, actor);
        }
        Write<int64_t>(out, order.targetPos.x.raw);
        Write<int64_t>(out, order.targetPos.y.raw);
        Write<int64_t>(out, order.targetPos.z.raw);
        Write<Sim::FActorId>(out, order.targetActor);
        Write<uint16_t>(out, order.produceTypeId);
        return out;
    }

    std::optional<FOrder> DeserializeOrder(std::span<const uint8_t> bytes)
    {
        size_t cursor = 0;
        uint8_t type = 0;
        uint8_t playerId = 0;
        uint32_t issueTick = 0;
        uint32_t actorCount = 0;

        if (!Read(bytes, cursor, type) || !Read(bytes, cursor, playerId) ||
            !Read(bytes, cursor, issueTick) || !Read(bytes, cursor, actorCount))
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
            if (!Read(bytes, cursor, actor))
            {
                return std::nullopt;
            }
        }

        int64_t x = 0;
        int64_t y = 0;
        int64_t z = 0;
        if (!Read(bytes, cursor, x) || !Read(bytes, cursor, y) || !Read(bytes, cursor, z) ||
            !Read(bytes, cursor, order.targetActor) || !Read(bytes, cursor, order.produceTypeId))
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
