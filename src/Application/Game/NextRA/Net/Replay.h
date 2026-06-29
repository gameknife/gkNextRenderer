#pragma once

#include "Net/Order.h"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace NextRA::Net
{
    struct FReplayTick
    {
        uint32_t tick = 0;
        std::vector<FOrder> orders;
        uint64_t syncHash = 0;
    };

    struct FReplay
    {
        uint64_t seed = 0;
        uint32_t latency = 0;
        std::vector<FReplayTick> ticks;
    };

    std::vector<uint8_t> SerializeReplay(const FReplay& replay);
    std::optional<FReplay> DeserializeReplay(std::span<const uint8_t> bytes);
}
