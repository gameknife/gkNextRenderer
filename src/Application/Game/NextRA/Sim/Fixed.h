#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <compare>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace NextRA::Sim
{
    struct FFixed
    {
        static constexpr int shift = 16;
        static constexpr int64_t oneRaw = int64_t{1} << shift;

        int64_t raw = 0;

        constexpr FFixed() = default;
        explicit constexpr FFixed(int64_t rawValue) : raw(rawValue) {}

        static constexpr FFixed FromRaw(int64_t rawValue) { return FFixed(rawValue); }
        static constexpr FFixed FromInt(int32_t value) { return FFixed(int64_t{value} << shift); }

        int32_t ToInt() const { return static_cast<int32_t>(raw >> shift); }
        float ToFloat() const { return static_cast<float>(raw) / static_cast<float>(oneRaw); }

        constexpr FFixed operator+() const { return *this; }
        constexpr FFixed operator-() const { return FFixed(-raw); }

        constexpr FFixed& operator+=(FFixed other)
        {
            raw += other.raw;
            return *this;
        }

        constexpr FFixed& operator-=(FFixed other)
        {
            raw -= other.raw;
            return *this;
        }

        constexpr FFixed& operator*=(FFixed other)
        {
            raw = (raw * other.raw) >> shift;
            return *this;
        }

        constexpr FFixed& operator/=(FFixed other)
        {
            raw = (raw << shift) / other.raw;
            return *this;
        }

        constexpr auto operator<=>(const FFixed&) const = default;
    };

    constexpr FFixed operator+(FFixed lhs, FFixed rhs)
    {
        lhs += rhs;
        return lhs;
    }

    constexpr FFixed operator-(FFixed lhs, FFixed rhs)
    {
        lhs -= rhs;
        return lhs;
    }

    constexpr FFixed operator*(FFixed lhs, FFixed rhs)
    {
        lhs *= rhs;
        return lhs;
    }

    constexpr FFixed operator/(FFixed lhs, FFixed rhs)
    {
        lhs /= rhs;
        return lhs;
    }

    constexpr FFixed Abs(FFixed value)
    {
        return value.raw < 0 ? FFixed::FromRaw(-value.raw) : value;
    }

    inline FFixed Sqrt(FFixed value)
    {
        if (value.raw <= 0)
        {
            return FFixed{};
        }

        const uint64_t target = static_cast<uint64_t>(value.raw) << FFixed::shift;
        uint64_t low = 0;
        uint64_t high = std::min<uint64_t>(target, uint64_t{std::numeric_limits<int32_t>::max()} << FFixed::shift);
        uint64_t answer = 0;

        while (low <= high)
        {
            const uint64_t mid = low + ((high - low) >> 1);
            const uint64_t quotient = mid == 0 ? 0 : target / mid;
            if (mid <= quotient)
            {
                answer = mid;
                low = mid + 1;
            }
            else
            {
                high = mid - 1;
            }
        }

        return FFixed::FromRaw(static_cast<int64_t>(answer));
    }
}
