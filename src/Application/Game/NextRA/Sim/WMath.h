#pragma once

#include "Sim/Fixed.h"

#include <array>
#include <cstdint>

namespace NextRA::Sim
{
    inline constexpr int cellSubUnits = 1024;
    inline constexpr int angleUnits = 4096;
    using WDist = FFixed;

    struct CPos
    {
        int32_t x = 0;
        int32_t z = 0;

        constexpr auto operator<=>(const CPos&) const = default;
    };

    struct WAngle
    {
        int32_t value = 0;

        static constexpr WAngle FromRaw(int32_t raw)
        {
            int32_t wrapped = raw % angleUnits;
            if (wrapped < 0)
            {
                wrapped += angleUnits;
            }
            return WAngle{wrapped};
        }
    };

    namespace Detail
    {
        constexpr FFixed SinApproxHalfTurn(int32_t phase)
        {
            constexpr int32_t halfTurn = angleUnits / 2;
            if (phase <= 0 || phase >= halfTurn)
            {
                return FFixed{};
            }

            const int64_t x = phase;
            const int64_t px = halfTurn - phase;
            const int64_t numerator = 16 * x * px * FFixed::oneRaw;
            const int64_t denominator = 5 * int64_t{halfTurn} * halfTurn - 4 * x * px;
            return FFixed::FromRaw(numerator / denominator);
        }

        constexpr std::array<FFixed, angleUnits> MakeSinTable()
        {
            std::array<FFixed, angleUnits> values{};
            for (int32_t i = 0; i < angleUnits; ++i)
            {
                if (i <= angleUnits / 2)
                {
                    values[static_cast<size_t>(i)] = SinApproxHalfTurn(i);
                }
                else
                {
                    values[static_cast<size_t>(i)] = -SinApproxHalfTurn(i - angleUnits / 2);
                }
            }
            return values;
        }
    }

    inline constexpr std::array<FFixed, angleUnits> sinTable = Detail::MakeSinTable();

    inline constexpr FFixed Sin(WAngle angle)
    {
        return sinTable[static_cast<size_t>(WAngle::FromRaw(angle.value).value)];
    }

    inline constexpr FFixed Cos(WAngle angle)
    {
        return Sin(WAngle::FromRaw(angle.value + angleUnits / 4));
    }

    struct WVec
    {
        FFixed x;
        FFixed y;
        FFixed z;
    };

    struct WPos
    {
        FFixed x;
        FFixed y;
        FFixed z;

        static constexpr WPos FromCells(int32_t cellX, int32_t cellZ)
        {
            return WPos{
                FFixed::FromInt(cellX * cellSubUnits),
                FFixed::FromInt(0),
                FFixed::FromInt(cellZ * cellSubUnits),
            };
        }

        CPos ToCell() const
        {
            return CPos{x.ToInt() / cellSubUnits, z.ToInt() / cellSubUnits};
        }
    };

    inline WVec operator-(const WPos& lhs, const WPos& rhs)
    {
        return WVec{lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
    }

    inline WPos operator+(const WPos& lhs, const WVec& rhs)
    {
        return WPos{lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
    }

    inline WVec operator*(const WVec& lhs, FFixed rhs)
    {
        return WVec{lhs.x * rhs, lhs.y * rhs, lhs.z * rhs};
    }

    inline FFixed LengthSquared2D(const WVec& value)
    {
        return value.x * value.x + value.z * value.z;
    }

    inline FFixed Length2D(const WVec& value)
    {
        return Sqrt(LengthSquared2D(value));
    }

    inline WVec Normalize2DOrZero(const WVec& value)
    {
        const FFixed length = Length2D(value);
        if (length.raw <= 0)
        {
            return WVec{};
        }
        return WVec{value.x / length, FFixed{}, value.z / length};
    }

    inline WPos MoveTowards2D(const WPos& from, const WPos& to, FFixed maxDistance)
    {
        const WVec delta = to - from;
        const FFixed distance = Length2D(delta);
        if (distance <= maxDistance || distance.raw <= 0)
        {
            return to;
        }

        const WVec dir = Normalize2DOrZero(delta);
        return from + dir * maxDistance;
    }

    inline bool SamePos2D(const WPos& lhs, const WPos& rhs)
    {
        return lhs.x == rhs.x && lhs.z == rhs.z;
    }

    inline constexpr FFixed CellDistance(int32_t cells)
    {
        return FFixed::FromInt(cells * cellSubUnits);
    }

    namespace Detail
    {
        constexpr int64_t AbsRaw(int64_t value)
        {
            return value < 0 ? -value : value;
        }

        constexpr int32_t AtanUnitsFromRatio(int64_t numerator, int64_t denominator)
        {
            if (numerator <= 0 || denominator <= 0)
            {
                return 0;
            }

            constexpr int64_t q = FFixed::oneRaw;
            const int64_t ratio = (numerator << FFixed::shift) / denominator;
            const int64_t curve = int64_t{512} * q + int64_t{178} * (q - ratio);
            return static_cast<int32_t>((ratio * curve) >> (FFixed::shift * 2));
        }
    }

    inline constexpr WAngle Atan2FromVec2(FFixed x, FFixed z)
    {
        const int64_t ax = Detail::AbsRaw(x.raw);
        const int64_t az = Detail::AbsRaw(z.raw);
        if (ax == 0 && az == 0)
        {
            return WAngle{};
        }

        int32_t base = 0;
        if (ax <= az)
        {
            base = Detail::AtanUnitsFromRatio(ax, az);
        }
        else
        {
            base = angleUnits / 4 - Detail::AtanUnitsFromRatio(az, ax);
        }

        if (z.raw >= 0)
        {
            return WAngle::FromRaw(x.raw >= 0 ? base : angleUnits - base);
        }
        return WAngle::FromRaw(x.raw >= 0 ? angleUnits / 2 - base : angleUnits / 2 + base);
    }

    inline constexpr int32_t ShortestAngleDiff(WAngle target, WAngle current)
    {
        int32_t delta = WAngle::FromRaw(target.value).value - WAngle::FromRaw(current.value).value;
        if (delta > angleUnits / 2)
        {
            delta -= angleUnits;
        }
        else if (delta < -angleUnits / 2)
        {
            delta += angleUnits;
        }
        return delta;
    }

    inline constexpr WAngle TurnToward(WAngle current, WAngle target, WAngle maxStep)
    {
        const int32_t step = WAngle::FromRaw(maxStep.value).value;
        if (step <= 0)
        {
            return WAngle::FromRaw(current.value);
        }

        const int32_t delta = ShortestAngleDiff(target, current);
        if (delta > step)
        {
            return WAngle::FromRaw(current.value + step);
        }
        if (delta < -step)
        {
            return WAngle::FromRaw(current.value - step);
        }
        return WAngle::FromRaw(target.value);
    }

}
