#include "Net/Replay.h"
#include "Net/BinaryCodec.h"

namespace NextRA::Net
{
    namespace
    {
        constexpr uint32_t replayMagic = 0x5052414e; // NRAP little-endian.
        constexpr uint32_t replayVersion = 1;

    }

    std::vector<uint8_t> SerializeReplay(const FReplay& replay)
    {
        std::vector<uint8_t> out;
        WriteBinary<uint32_t>(out, replayMagic);
        WriteBinary<uint32_t>(out, replayVersion);
        WriteBinary<uint64_t>(out, replay.seed);
        WriteBinary<uint32_t>(out, replay.latency);
        WriteBinary<uint32_t>(out, static_cast<uint32_t>(replay.ticks.size()));
        for (const FReplayTick& tick : replay.ticks)
        {
            WriteBinary<uint32_t>(out, tick.tick);
            WriteBinary<uint64_t>(out, tick.syncHash);
            WriteBinary<uint32_t>(out, static_cast<uint32_t>(tick.orders.size()));
            for (const FOrder& order : tick.orders)
            {
                const std::vector<uint8_t> orderBytes = SerializeOrder(order);
                WriteBinary<uint32_t>(out, static_cast<uint32_t>(orderBytes.size()));
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
        if (!ReadBinary(bytes, cursor, magic) || !ReadBinary(bytes, cursor, version) ||
            magic != replayMagic || version != replayVersion ||
            !ReadBinary(bytes, cursor, replay.seed) || !ReadBinary(bytes, cursor, replay.latency) ||
            !ReadBinary(bytes, cursor, tickCount))
        {
            return std::nullopt;
        }

        replay.ticks.resize(tickCount);
        for (FReplayTick& tick : replay.ticks)
        {
            uint32_t orderCount = 0;
            if (!ReadBinary(bytes, cursor, tick.tick) || !ReadBinary(bytes, cursor, tick.syncHash) ||
                !ReadBinary(bytes, cursor, orderCount))
            {
                return std::nullopt;
            }
            tick.orders.reserve(orderCount);
            for (uint32_t index = 0; index < orderCount; ++index)
            {
                uint32_t orderSize = 0;
                if (!ReadBinary(bytes, cursor, orderSize) || cursor + orderSize > bytes.size())
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
