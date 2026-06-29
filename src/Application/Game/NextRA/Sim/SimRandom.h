#pragma once

#include <cstdint>

namespace NextRA::Sim
{
    class FSimRandom
    {
    public:
        explicit FSimRandom(uint64_t seed = 0x4E65787452415369ull) :
            state_(seed == 0 ? 0x4E65787452415369ull : seed)
        {
        }

        uint32_t NextU32()
        {
            uint64_t x = state_;
            x ^= x << 13;
            x ^= x >> 7;
            x ^= x << 17;
            state_ = x;
            return static_cast<uint32_t>(x >> 32);
        }

        uint64_t State() const { return state_; }
        void SetState(uint64_t state) { state_ = state == 0 ? 0x4E65787452415369ull : state; }

    private:
        uint64_t state_ = 0x4E65787452415369ull;
    };
}
