#include "Net/Replay.h"

#include <cstring>

namespace NextRA::Net
{
    namespace
    {
        constexpr uint32_t replayMagic = 0x5052414e; // NRAP little-endian.
        constexpr uint32_t replayVersion = 1;

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

    std::vector<uint8_t> SerializeReplay(const FReplay& replay)
    {
        std::vector<uint8_t> out;
        Write<uint32_t>(out, replayMagic);
        Write<uint32_t>(out, replayVersion);
        Write<uint64_t>(out, replay.seed);
        Write<uint32_t>(out, replay.latency);
        Write<uint32_t>(out, static_cast<uint32_t>(replay.ticks.size()));
        for (const FReplayTick& tick : replay.ticks)
        {
            Write<uint32_t>(out, tick.tick);
            Write<uint64_t>(out, tick.syncHash);
            Write<uint32_t>(out, static_cast<uint32_t>(tick.orders.size()));
            for (const FOrder& order : tick.orders)
            {
                const std::vector<uint8_t> orderBytes = SerializeOrder(order);
                Write<uint32_t>(out, static_cast<uint32_t>(orderBytes.size()));
                out.insert(out.end(), orderBytes.begin(), orderBytes.end());
            }
        }
        return out;
    }

    std::optional<FReplay> DeserializeReplay(std::span<const uint8_t> bytes)
    {
        size_t cursor = 0;
        uint32_t magic = 0;
        uint32_t version = 0;
        FReplay replay;
        uint32_t tickCount = 0;
        if (!Read(bytes, cursor, magic) || !Read(bytes, cursor, version) ||
            magic != replayMagic || version != replayVersion ||
            !Read(bytes, cursor, replay.seed) || !Read(bytes, cursor, replay.latency) ||
            !Read(bytes, cursor, tickCount))
        {
            return std::nullopt;
        }

        replay.ticks.resize(tickCount);
        for (FReplayTick& tick : replay.ticks)
        {
            uint32_t orderCount = 0;
            if (!Read(bytes, cursor, tick.tick) || !Read(bytes, cursor, tick.syncHash) ||
                !Read(bytes, cursor, orderCount))
            {
                return std::nullopt;
            }
            tick.orders.reserve(orderCount);
            for (uint32_t index = 0; index < orderCount; ++index)
            {
                uint32_t orderSize = 0;
                if (!Read(bytes, cursor, orderSize) || cursor + orderSize > bytes.size())
                {
                    return std::nullopt;
                }
                std::optional<FOrder> order = DeserializeOrder(bytes.subspan(cursor, orderSize));
                if (!order)
                {
                    return std::nullopt;
                }
                cursor += orderSize;
                tick.orders.push_back(std::move(*order));
            }
        }

        if (cursor != bytes.size())
        {
            return std::nullopt;
        }
        return replay;
    }
}
