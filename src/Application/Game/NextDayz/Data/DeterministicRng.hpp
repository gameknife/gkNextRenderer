#pragma once

#include "Engine/Common/CoreMinimal.hpp"

namespace NextDayz
{
    class FDeterministicRng
    {
    public:
        explicit FDeterministicRng(uint64_t seed = 0x4E445AULL)
            : seed_(seed == 0 ? 1 : seed), state_(seed_)
        {
        }

        uint64_t Seed() const { return seed_; }

        void Reset(uint64_t seed)
        {
            seed_ = seed == 0 ? 1 : seed;
            state_ = seed_;
        }

        uint32_t NextU32()
        {
            uint64_t value = (state_ += 0x9e3779b97f4a7c15ULL);
            value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
            return static_cast<uint32_t>((value ^ (value >> 31U)) >> 32U);
        }

        float UnitFloat()
        {
            return static_cast<float>(NextU32() >> 8U) * (1.0f / 16777216.0f);
        }

        uint32_t Range(uint32_t upperExclusive)
        {
            return upperExclusive == 0 ? 0 : NextU32() % upperExclusive;
        }

        FDeterministicRng Derive(uint64_t systemTag) const
        {
            uint64_t value = seed_ ^ (systemTag + 0x9e3779b97f4a7c15ULL + (seed_ << 6U) + (seed_ >> 2U));
            value ^= value >> 30U;
            value *= 0xbf58476d1ce4e5b9ULL;
            value ^= value >> 27U;
            value *= 0x94d049bb133111ebULL;
            return FDeterministicRng(value ^ (value >> 31U));
        }

    private:
        uint64_t seed_ = 1;
        uint64_t state_ = 1;
    };
}
