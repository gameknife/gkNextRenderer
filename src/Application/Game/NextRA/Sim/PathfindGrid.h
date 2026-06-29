#pragma once

#include "Sim/WMath.h"

#include <optional>
#include <vector>

namespace NextRA::Sim
{
    class FPathfindGrid
    {
    public:
        FPathfindGrid(int32_t width, int32_t height, CPos origin);

        bool IsInside(CPos cell) const;
        bool IsPassable(CPos cell) const;
        void SetBlocked(CPos cell, bool blocked);
        std::vector<CPos> FindPath(CPos start, CPos goal) const;
        int32_t Width() const { return width_; }
        int32_t Height() const { return height_; }
        CPos Origin() const { return origin_; }

    private:
        int32_t ToIndex(CPos cell) const;

        int32_t width_ = 0;
        int32_t height_ = 0;
        CPos origin_;
        std::vector<uint8_t> blocked_;
    };
}
