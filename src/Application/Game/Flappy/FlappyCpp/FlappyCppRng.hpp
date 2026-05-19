#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <cstdint>

namespace Flappy
{
    class FXorShift32 final
    {
    public:
        explicit FXorShift32(uint32_t seed = 0x00C0FFEEu) : state_(seed == 0 ? 0x00C0FFEEu : seed) {}

        uint32_t NextU32()
        {
            uint32_t x = state_;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            state_ = x;
            return x;
        }

        float NextFloat01()
        {
            constexpr float invMax = 1.0f / 4294967295.0f;
            return static_cast<float>(NextU32()) * invMax;
        }

        uint32_t GetState() const { return state_; }
        void Reset(uint32_t seed) { state_ = seed == 0 ? 0x00C0FFEEu : seed; }

    private:
        uint32_t state_;
    };

    // Seed 0xC0FFEE first 10 outputs:
    // 4170923632, 1979402906, 2309528846, 3677032778, 2571351211,
    // 352504640, 3241624533, 15931667, 435705755, 1919745502
}
