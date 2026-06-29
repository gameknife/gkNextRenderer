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

    inline FFixed CellDistance(int32_t cells)
    {
        return FFixed::FromInt(cells * cellSubUnits);
    }

}
