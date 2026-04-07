#include "Runtime/AI/NavGrid.h"
#include "Assets/Acceleration/CPUAccelerationStructure.h"
#include "Assets/GPU/UniformBuffer.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

#include <spdlog/spdlog.h>

namespace
{
    constexpr int kDirX[] = {1, -1, 0, 0, 1, -1, 1, -1};
    constexpr int kDirZ[] = {0, 0, 1, -1, 1, -1, -1, 1};
}

void FNavGrid::Build(FCPUAccelerationStructure& bvh, const FNavGridSettings& settings)
{
    settings_ = settings;
    width_ = static_cast<int>(std::ceil((settings_.worldMax.x - settings_.worldMin.x) / settings_.cellSize));
    height_ = static_cast<int>(std::ceil((settings_.worldMax.z - settings_.worldMin.z) / settings_.cellSize));

    if (width_ <= 0 || height_ <= 0)
    {
        cells_.clear();
        spdlog::warn("NavGrid::Build - invalid bounds, grid not built");
        return;
    }

    cells_.resize(width_ * height_);

    const float maxSlopeCos = std::cos(glm::radians(settings_.maxSlopeAngle));
    int walkableCount = 0;

    for (int gz = 0; gz < height_; ++gz)
    {
        for (int gx = 0; gx < width_; ++gx)
        {
            FNavCell& cell = cells_[CellIndex(gx, gz)];
            cell.walkable = false;

            const glm::vec3 worldCenter = GridToWorld(gx, gz);
            const glm::vec3 rayOrigin(worldCenter.x, settings_.sampleCeiling, worldCenter.z);
            const glm::vec3 rayDir(0.0f, -1.0f, 0.0f);

            Assets::RayCastResult hit = bvh.RayCastInCPU(rayOrigin, rayDir);
            if (!hit.Hitted)
            {
                continue;
            }

            const float groundY = rayOrigin.y - hit.T;
            cell.groundHeight = groundY;

            const glm::vec3 normal(hit.Normal.x, hit.Normal.y, hit.Normal.z);
            const float slopeCos = glm::dot(normal, glm::vec3(0.0f, 1.0f, 0.0f));
            if (slopeCos < maxSlopeCos)
            {
                continue;
            }

            const glm::vec3 clearOrigin(worldCenter.x, groundY + 0.1f, worldCenter.z);
            const glm::vec3 upDir(0.0f, 1.0f, 0.0f);
            Assets::RayCastResult clearHit = bvh.RayCastInCPU(clearOrigin, upDir);
            if (clearHit.Hitted && clearHit.T < settings_.clearanceHeight)
            {
                continue;
            }

            cell.walkable = true;
            ++walkableCount;
        }
    }

    // Edge erosion: mark walkable cells adjacent to blocked cells as blocked
    std::vector<bool> eroded(cells_.size(), false);
    for (int gz = 0; gz < height_; ++gz)
    {
        for (int gx = 0; gx < width_; ++gx)
        {
            if (!cells_[CellIndex(gx, gz)].walkable)
            {
                continue;
            }
            for (int d = 0; d < 8; ++d)
            {
                int nx = gx + kDirX[d];
                int nz = gz + kDirZ[d];
                if (!InBounds(nx, nz) || !cells_[CellIndex(nx, nz)].walkable)
                {
                    eroded[CellIndex(gx, gz)] = true;
                    break;
                }
            }
        }
    }

    int erodedCount = 0;
    for (int i = 0; i < static_cast<int>(cells_.size()); ++i)
    {
        if (eroded[i])
        {
            cells_[i].walkable = false;
            ++erodedCount;
        }
    }

    spdlog::info("NavGrid built: {}x{} cells, {} walkable ({} eroded), cellSize={:.2f}m",
                 width_, height_, walkableCount - erodedCount, erodedCount, settings_.cellSize);
}

std::vector<glm::vec3> FNavGrid::FindPath(const glm::vec3& from, const glm::vec3& to, float referenceHeight) const
{
    if (cells_.empty())
    {
        return {};
    }

    glm::ivec2 startGrid = WorldToGrid(from);
    glm::ivec2 goalGrid = WorldToGrid(to);

    if (!InBounds(startGrid.x, startGrid.y) ||
        !cells_[CellIndex(startGrid.x, startGrid.y)].walkable)
    {
        startGrid = FindNearestWalkable(startGrid, referenceHeight);
    }
    if (!InBounds(goalGrid.x, goalGrid.y) ||
        !cells_[CellIndex(goalGrid.x, goalGrid.y)].walkable)
    {
        goalGrid = FindNearestWalkable(goalGrid, to.y);
    }

    if (startGrid.x < 0 || goalGrid.x < 0)
    {
        return {};
    }

    if (startGrid == goalGrid)
    {
        return {GridToWorld(startGrid.x, startGrid.y)};
    }

    const int totalCells = width_ * height_;
    std::vector<float> gCost(totalCells, std::numeric_limits<float>::max());
    std::vector<int> parent(totalCells, -1);

    struct FOpenNode
    {
        float fCost;
        int index;
        bool operator>(const FOpenNode& other) const { return fCost > other.fCost; }
    };

    std::priority_queue<FOpenNode, std::vector<FOpenNode>, std::greater<FOpenNode>> openList;

    const int startIdx = CellIndex(startGrid.x, startGrid.y);
    const int goalIdx = CellIndex(goalGrid.x, goalGrid.y);
    const glm::vec3 goalWorld = GridToWorld(goalGrid.x, goalGrid.y);
    gCost[startIdx] = 0.0f;
    openList.push({glm::distance(GridToWorld(startGrid.x, startGrid.y), goalWorld), startIdx});

    bool found = false;

    while (!openList.empty())
    {
        const FOpenNode current = openList.top();
        openList.pop();

        if (current.index == goalIdx)
        {
            found = true;
            break;
        }

        const int cx = current.index % width_;
        const int cz = current.index / width_;
        const float currentHeuristic = glm::distance(GridToWorld(cx, cz), goalWorld);
        if (current.fCost > gCost[current.index] + currentHeuristic + 0.001f)
        {
            continue;
        }

        for (int d = 0; d < 8; ++d)
        {
            const int nx = cx + kDirX[d];
            const int nz = cz + kDirZ[d];

            if (!InBounds(nx, nz))
            {
                continue;
            }

            const int nIdx = CellIndex(nx, nz);
            if (!cells_[nIdx].walkable || !CanTraverseByIndex(current.index, nIdx))
            {
                continue;
            }

            // Prevent diagonal corner cutting
            if (d >= 4)
            {
                const int adj1x = cx + kDirX[d];
                const int adj1z = cz;
                const int adj2x = cx;
                const int adj2z = cz + kDirZ[d];
                if (!CanTraverse(cx, cz, adj1x, adj1z) ||
                    !CanTraverse(cx, cz, adj2x, adj2z) ||
                    !CanTraverse(adj1x, adj1z, nx, nz) ||
                    !CanTraverse(adj2x, adj2z, nx, nz))
                {
                    continue;
                }
            }

            const float moveCost = glm::distance(GridToWorld(cx, cz), GridToWorld(nx, nz));
            const float newG = gCost[current.index] + moveCost;

            if (newG < gCost[nIdx])
            {
                gCost[nIdx] = newG;
                parent[nIdx] = current.index;
                const float h = glm::distance(GridToWorld(nx, nz), goalWorld);
                openList.push({newG + h, nIdx});
            }
        }
    }

    if (!found)
    {
        return {};
    }

    // Reconstruct grid path
    std::vector<glm::ivec2> gridPath;
    for (int idx = goalIdx; idx != -1; idx = parent[idx])
    {
        gridPath.push_back({idx % width_, idx / width_});
    }
    std::reverse(gridPath.begin(), gridPath.end());

    // Smooth path
    SmoothPath(gridPath);

    // Convert to world positions
    std::vector<glm::vec3> worldPath;
    worldPath.reserve(gridPath.size());
    for (const auto& gp : gridPath)
    {
        worldPath.push_back(GridToWorld(gp.x, gp.y));
    }

    return worldPath;
}

bool FNavGrid::IsWalkable(const glm::vec3& worldPos) const
{
    const glm::ivec2 g = WorldToGrid(worldPos);
    if (!InBounds(g.x, g.y))
    {
        return false;
    }
    return cells_[CellIndex(g.x, g.y)].walkable;
}

bool FNavGrid::IsCellWalkable(int gx, int gz) const
{
    return InBounds(gx, gz) && cells_[CellIndex(gx, gz)].walkable;
}

bool FNavGrid::IsCellReachable(int gx, int gz, float referenceHeight) const
{
    if (!InBounds(gx, gz) || !cells_[CellIndex(gx, gz)].walkable)
    {
        return false;
    }
    return std::abs(cells_[CellIndex(gx, gz)].groundHeight - referenceHeight) <= settings_.floorHeightTolerance;
}

glm::vec3 FNavGrid::GetCellWorldPosition(int gx, int gz) const
{
    return GridToWorld(gx, gz);
}

glm::ivec2 FNavGrid::WorldToGrid(const glm::vec3& worldPos) const
{
    return {
        static_cast<int>(std::floor((worldPos.x - settings_.worldMin.x) / settings_.cellSize)),
        static_cast<int>(std::floor((worldPos.z - settings_.worldMin.z) / settings_.cellSize))
    };
}

glm::vec3 FNavGrid::GridToWorld(int gx, int gz) const
{
    const float x = settings_.worldMin.x + (static_cast<float>(gx) + 0.5f) * settings_.cellSize;
    const float z = settings_.worldMin.z + (static_cast<float>(gz) + 0.5f) * settings_.cellSize;
    const float y = cells_[CellIndex(gx, gz)].groundHeight;
    return {x, y, z};
}

int FNavGrid::CellIndex(int gx, int gz) const
{
    return gz * width_ + gx;
}

bool FNavGrid::InBounds(int gx, int gz) const
{
    return gx >= 0 && gx < width_ && gz >= 0 && gz < height_;
}

glm::ivec2 FNavGrid::FindNearestWalkable(const glm::ivec2& cell, float referenceHeight) const
{
    constexpr int kMaxRadius = 5;
    float bestScore = std::numeric_limits<float>::max();
    glm::ivec2 bestCell(-1, -1);

    for (int r = 0; r <= kMaxRadius; ++r)
    {
        for (int dz = -r; dz <= r; ++dz)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                if (std::abs(dx) != r && std::abs(dz) != r)
                {
                    continue;
                }
                int nx = cell.x + dx;
                int nz = cell.y + dz;
                if (!InBounds(nx, nz))
                {
                    continue;
                }

                const FNavCell& candidate = cells_[CellIndex(nx, nz)];
                if (!candidate.walkable)
                {
                    continue;
                }

                const float horizontalDistance = glm::length(glm::vec2(static_cast<float>(dx), static_cast<float>(dz)));
                const float heightPenalty = std::abs(candidate.groundHeight - referenceHeight) /
                                            std::max(settings_.maxStepHeight, 0.05f);
                const float score = horizontalDistance + heightPenalty * 0.35f;
                if (score < bestScore)
                {
                    bestScore = score;
                    bestCell = {nx, nz};
                }
            }
        }

        if (bestCell.x >= 0)
        {
            return bestCell;
        }
    }

    return {-1, -1};
}

void FNavGrid::SmoothPath(std::vector<glm::ivec2>& gridPath) const
{
    if (gridPath.size() <= 2)
    {
        return;
    }

    std::vector<glm::ivec2> smoothed;
    smoothed.push_back(gridPath.front());

    size_t current = 0;
    while (current < gridPath.size() - 1)
    {
        size_t farthest = current + 1;
        for (size_t j = gridPath.size() - 1; j > current + 1; --j)
        {
            if (GridLineWalkable(gridPath[current], gridPath[j]))
            {
                farthest = j;
                break;
            }
        }
        smoothed.push_back(gridPath[farthest]);
        current = farthest;
    }

    gridPath = std::move(smoothed);
}

bool FNavGrid::GridLineWalkable(const glm::ivec2& a, const glm::ivec2& b) const
{
    // Bresenham line check
    int x0 = a.x, z0 = a.y;
    int x1 = b.x, z1 = b.y;
    int dx = std::abs(x1 - x0);
    int dz = std::abs(z1 - z0);
    int sx = (x0 < x1) ? 1 : -1;
    int sz = (z0 < z1) ? 1 : -1;
    int err = dx - dz;
    int previousX = x0;
    int previousZ = z0;
    bool hasPrevious = false;

    while (true)
    {
        if (!InBounds(x0, z0) || !cells_[CellIndex(x0, z0)].walkable)
        {
            return false;
        }

        if (hasPrevious && !CanTraverse(previousX, previousZ, x0, z0))
        {
            return false;
        }

        if (x0 == x1 && z0 == z1)
        {
            break;
        }

        previousX = x0;
        previousZ = z0;
        hasPrevious = true;

        int e2 = 2 * err;
        if (e2 > -dz)
        {
            err -= dz;
            x0 += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            z0 += sz;
        }
    }
    return true;
}

std::vector<uint8_t> FNavGrid::BuildReachabilityMask(const glm::vec3& from, float referenceHeight) const
{
    std::vector<uint8_t> mask(cells_.size(), 0);
    if (cells_.empty())
    {
        return mask;
    }

    glm::ivec2 startGrid = WorldToGrid(from);
    if (!InBounds(startGrid.x, startGrid.y) ||
        !cells_[CellIndex(startGrid.x, startGrid.y)].walkable)
    {
        startGrid = FindNearestWalkable(startGrid, referenceHeight);
    }

    if (startGrid.x < 0)
    {
        return mask;
    }

    std::queue<int> open;
    const int startIdx = CellIndex(startGrid.x, startGrid.y);
    mask[startIdx] = 1;
    open.push(startIdx);

    while (!open.empty())
    {
        const int currentIdx = open.front();
        open.pop();

        const int cx = currentIdx % width_;
        const int cz = currentIdx / width_;
        for (int d = 0; d < 8; ++d)
        {
            const int nx = cx + kDirX[d];
            const int nz = cz + kDirZ[d];
            if (!InBounds(nx, nz))
            {
                continue;
            }

            const int nextIdx = CellIndex(nx, nz);
            if (mask[nextIdx] != 0 || !cells_[nextIdx].walkable || !CanTraverseByIndex(currentIdx, nextIdx))
            {
                continue;
            }

            if (d >= 4)
            {
                const int adj1x = cx + kDirX[d];
                const int adj1z = cz;
                const int adj2x = cx;
                const int adj2z = cz + kDirZ[d];
                if (!CanTraverse(cx, cz, adj1x, adj1z) ||
                    !CanTraverse(cx, cz, adj2x, adj2z) ||
                    !CanTraverse(adj1x, adj1z, nx, nz) ||
                    !CanTraverse(adj2x, adj2z, nx, nz))
                {
                    continue;
                }
            }

            mask[nextIdx] = 1;
            open.push(nextIdx);
        }
    }

    return mask;
}

bool FNavGrid::CanTraverse(int fromGx, int fromGz, int toGx, int toGz) const
{
    if (!InBounds(fromGx, fromGz) || !InBounds(toGx, toGz))
    {
        return false;
    }
    return CanTraverseByIndex(CellIndex(fromGx, fromGz), CellIndex(toGx, toGz));
}

bool FNavGrid::CanTraverseByIndex(int fromIdx, int toIdx) const
{
    if (fromIdx < 0 || toIdx < 0 ||
        fromIdx >= static_cast<int>(cells_.size()) || toIdx >= static_cast<int>(cells_.size()))
    {
        return false;
    }

    const FNavCell& fromCell = cells_[fromIdx];
    const FNavCell& toCell = cells_[toIdx];
    if (!fromCell.walkable || !toCell.walkable)
    {
        return false;
    }

    return std::abs(toCell.groundHeight - fromCell.groundHeight) <= settings_.maxStepHeight;
}
