#include "Sim/PathfindGrid.h"

#include <algorithm>
#include <limits>
#include <queue>

namespace NextRA::Sim
{
    namespace
    {
        int32_t AbsInt(int32_t value)
        {
            return value < 0 ? -value : value;
        }

        int32_t Heuristic(CPos a, CPos b)
        {
            return AbsInt(a.x - b.x) + AbsInt(a.z - b.z);
        }
    }

    FPathfindGrid::FPathfindGrid(int32_t width, int32_t height, CPos origin) :
        width_(width), height_(height), origin_(origin), blocked_(static_cast<size_t>(width * height), 0)
    {
    }

    bool FPathfindGrid::IsInside(CPos cell) const
    {
        const int32_t localX = cell.x - origin_.x;
        const int32_t localZ = cell.z - origin_.z;
        return localX >= 0 && localZ >= 0 && localX < width_ && localZ < height_;
    }

    bool FPathfindGrid::IsPassable(CPos cell) const
    {
        return IsInside(cell) && blocked_[static_cast<size_t>(ToIndex(cell))] == 0;
    }

    void FPathfindGrid::SetBlocked(CPos cell, bool blocked)
    {
        if (!IsInside(cell))
        {
            return;
        }
        blocked_[static_cast<size_t>(ToIndex(cell))] = blocked ? 1u : 0u;
    }

    int32_t FPathfindGrid::ToIndex(CPos cell) const
    {
        const int32_t localX = cell.x - origin_.x;
        const int32_t localZ = cell.z - origin_.z;
        return localZ * width_ + localX;
    }

    std::vector<CPos> FPathfindGrid::FindPath(CPos start, CPos goal) const
    {
        if (!IsPassable(start) || !IsPassable(goal))
        {
            return {};
        }
        if (start == goal)
        {
            return {start};
        }

        struct FOpenNode
        {
            CPos cell;
            int32_t f = 0;
            int32_t g = 0;
        };
        struct FCompare
        {
            bool operator()(const FOpenNode& lhs, const FOpenNode& rhs) const
            {
                if (lhs.f != rhs.f)
                {
                    return lhs.f > rhs.f;
                }
                if (lhs.g != rhs.g)
                {
                    return lhs.g < rhs.g;
                }
                if (lhs.cell.z != rhs.cell.z)
                {
                    return lhs.cell.z > rhs.cell.z;
                }
                return lhs.cell.x > rhs.cell.x;
            }
        };

        const int32_t total = width_ * height_;
        std::vector<int32_t> cameFrom(static_cast<size_t>(total), -1);
        std::vector<int32_t> bestG(static_cast<size_t>(total), std::numeric_limits<int32_t>::max());
        std::priority_queue<FOpenNode, std::vector<FOpenNode>, FCompare> open;

        const int32_t startIndex = ToIndex(start);
        bestG[static_cast<size_t>(startIndex)] = 0;
        open.push(FOpenNode{start, Heuristic(start, goal), 0});

        constexpr CPos dirs[] = {
            CPos{0, -1},
            CPos{-1, 0},
            CPos{1, 0},
            CPos{0, 1},
        };

        while (!open.empty())
        {
            const FOpenNode current = open.top();
            open.pop();

            const int32_t currentIndex = ToIndex(current.cell);
            if (current.g != bestG[static_cast<size_t>(currentIndex)])
            {
                continue;
            }
            if (current.cell == goal)
            {
                std::vector<CPos> path;
                int32_t cursor = currentIndex;
                while (cursor >= 0)
                {
                    const int32_t x = cursor % width_ + origin_.x;
                    const int32_t z = cursor / width_ + origin_.z;
                    path.push_back(CPos{x, z});
                    cursor = cameFrom[static_cast<size_t>(cursor)];
                }
                std::reverse(path.begin(), path.end());
                return path;
            }

            for (CPos dir : dirs)
            {
                const CPos next{current.cell.x + dir.x, current.cell.z + dir.z};
                if (!IsPassable(next))
                {
                    continue;
                }

                const int32_t nextIndex = ToIndex(next);
                const int32_t tentativeG = current.g + 1;
                if (tentativeG >= bestG[static_cast<size_t>(nextIndex)])
                {
                    continue;
                }

                cameFrom[static_cast<size_t>(nextIndex)] = currentIndex;
                bestG[static_cast<size_t>(nextIndex)] = tentativeG;
                open.push(FOpenNode{next, tentativeG + Heuristic(next, goal), tentativeG});
            }
        }

        return {};
    }
}
