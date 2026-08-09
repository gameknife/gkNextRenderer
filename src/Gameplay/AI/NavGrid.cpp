#include "Gameplay/AI/NavGrid.h"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <queue>

#include <spdlog/spdlog.h>

namespace
{
    constexpr int kDirX[] = {1, -1, 0, 0, 1, -1, 1, -1};
    constexpr int kDirZ[] = {0, 0, 1, -1, 1, -1, -1, 1};
    constexpr float kDiagonalInvSqrt = 0.70710678f;
}

namespace NextGameplay
{
void FNavGrid::Build(Assets::CPU::FCPUAccelerationStructure& bvh, const FNavGridSettings& settings)
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

    cells_.assign(width_ * height_, {});

    const FGridRect fullRect{0, 0, width_ - 1, height_ - 1};
    SampleBaseWalkability(bvh, fullRect);
    UpdateWalkabilityFromBase(fullRect);

    int walkableCount = 0;
    for (const FNavCell& cell : cells_)
    {
        if (cell.walkable)
        {
            ++walkableCount;
        }
    }

    spdlog::info("NavGrid built: {}x{} cells, {} walkable, cellSize={:.2f}m, agentRadius={:.2f}m",
                 width_, height_, walkableCount, settings_.cellSize, settings_.agentRadius);
}

void FNavGrid::MaskUnwalkable(const std::function<bool(const glm::vec3&)>& predicate)
{
    if (cells_.empty() || !predicate)
    {
        return;
    }
    int maskedCount = 0;
    for (int gz = 0; gz < height_; ++gz)
    {
        for (int gx = 0; gx < width_; ++gx)
        {
            FNavCell& cell = cells_[CellIndex(gx, gz)];
            if (cell.baseWalkable && predicate(GetCellWorldPosition(gx, gz)))
            {
                cell.baseWalkable = false;
                ++maskedCount;
            }
        }
    }
    // Re-derive walkable from base so agent-radius erosion respects the mask.
    const FGridRect fullRect{0, 0, width_ - 1, height_ - 1};
    UpdateWalkabilityFromBase(fullRect);
    spdlog::info("NavGrid mask: {} cells vetoed", maskedCount);
}

void FNavGrid::RebuildDirtyRegion(Assets::CPU::FCPUAccelerationStructure& bvh, const glm::vec3& dirtyWorldMin,
                                  const glm::vec3& dirtyWorldMax)
{
    if (cells_.empty() || width_ <= 0 || height_ <= 0)
    {
        return;
    }

    const FGridRect dirtyRect = MakeGridRectFromWorldBounds(dirtyWorldMin, dirtyWorldMax);
    if (!IsGridRectValid(dirtyRect))
    {
        return;
    }

    const FGridRect changedBaseRect = ExpandGridRect(dirtyRect, GetFootprintMarginCells());
    if (!IsGridRectValid(changedBaseRect))
    {
        return;
    }

    SampleBaseWalkability(bvh, changedBaseRect);

    const FGridRect walkableRect = ExpandGridRect(changedBaseRect, GetExtraErosionCells());
    UpdateWalkabilityFromBase(walkableRect);

    spdlog::info("NavGrid partial rebuild: worldMin=({:.2f}, {:.2f}, {:.2f}) worldMax=({:.2f}, {:.2f}, {:.2f}) grid=[{},{}]-[{},{}]",
                 dirtyWorldMin.x, dirtyWorldMin.y, dirtyWorldMin.z,
                 dirtyWorldMax.x, dirtyWorldMax.y, dirtyWorldMax.z,
                 walkableRect.minGx, walkableRect.minGz, walkableRect.maxGx, walkableRect.maxGz);
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

FNavGrid::FGridRect FNavGrid::MakeGridRectFromWorldBounds(const glm::vec3& worldMin, const glm::vec3& worldMax) const
{
    if (cells_.empty())
    {
        return {};
    }

    const glm::vec3 clampedMin(
        glm::clamp(std::min(worldMin.x, worldMax.x), settings_.worldMin.x, settings_.worldMax.x),
        0.0f,
        glm::clamp(std::min(worldMin.z, worldMax.z), settings_.worldMin.z, settings_.worldMax.z));
    const glm::vec3 clampedMax(
        glm::clamp(std::max(worldMin.x, worldMax.x), settings_.worldMin.x, settings_.worldMax.x),
        0.0f,
        glm::clamp(std::max(worldMin.z, worldMax.z), settings_.worldMin.z, settings_.worldMax.z));

    const int minGx =
        static_cast<int>(std::floor((clampedMin.x - settings_.worldMin.x) / settings_.cellSize));
    const int minGz =
        static_cast<int>(std::floor((clampedMin.z - settings_.worldMin.z) / settings_.cellSize));
    const int maxGx =
        static_cast<int>(std::floor((clampedMax.x - settings_.worldMin.x) / settings_.cellSize));
    const int maxGz =
        static_cast<int>(std::floor((clampedMax.z - settings_.worldMin.z) / settings_.cellSize));

    return ExpandGridRect({minGx, minGz, maxGx, maxGz}, 0);
}

FNavGrid::FGridRect FNavGrid::ExpandGridRect(const FGridRect& rect, int margin) const
{
    if (width_ <= 0 || height_ <= 0)
    {
        return {};
    }

    FGridRect expanded;
    expanded.minGx = glm::clamp(rect.minGx - margin, 0, width_ - 1);
    expanded.minGz = glm::clamp(rect.minGz - margin, 0, height_ - 1);
    expanded.maxGx = glm::clamp(rect.maxGx + margin, 0, width_ - 1);
    expanded.maxGz = glm::clamp(rect.maxGz + margin, 0, height_ - 1);
    return expanded;
}

bool FNavGrid::IsGridRectValid(const FGridRect& rect) const
{
    return rect.minGx <= rect.maxGx && rect.minGz <= rect.maxGz;
}

void FNavGrid::SampleBaseWalkability(Assets::CPU::FCPUAccelerationStructure& bvh, const FGridRect& rect)
{
    if (!IsGridRectValid(rect))
    {
        return;
    }

    for (int gz = rect.minGz; gz <= rect.maxGz; ++gz)
    {
        for (int gx = rect.minGx; gx <= rect.maxGx; ++gx)
        {
            FNavCell sampledCell;
            SampleBaseCell(bvh, gx, gz, sampledCell);
            cells_[CellIndex(gx, gz)] = sampledCell;
        }
    }
}

void FNavGrid::UpdateWalkabilityFromBase(const FGridRect& rect)
{
    if (!IsGridRectValid(rect))
    {
        return;
    }

    const int erosionCells = GetExtraErosionCells();
    for (int gz = rect.minGz; gz <= rect.maxGz; ++gz)
    {
        for (int gx = rect.minGx; gx <= rect.maxGx; ++gx)
        {
            FNavCell& cell = cells_[CellIndex(gx, gz)];
            cell.walkable = cell.baseWalkable;
            if (!cell.baseWalkable)
            {
                continue;
            }

            bool blockedByNeighbor = false;
            for (int dz = -erosionCells; dz <= erosionCells && !blockedByNeighbor; ++dz)
            {
                for (int dx = -erosionCells; dx <= erosionCells; ++dx)
                {
                    if (dx == 0 && dz == 0)
                    {
                        continue;
                    }

                    const int nx = gx + dx;
                    const int nz = gz + dz;
                    if (!InBounds(nx, nz) || !cells_[CellIndex(nx, nz)].baseWalkable)
                    {
                        blockedByNeighbor = true;
                        break;
                    }
                }
            }

            if (blockedByNeighbor)
            {
                cell.walkable = false;
            }
        }
    }
}

bool FNavGrid::SampleBaseCell(Assets::CPU::FCPUAccelerationStructure& bvh, int gx, int gz, FNavCell& outCell) const
{
    outCell = {};

    const glm::vec3 worldCenter = GridToWorld(gx, gz);
    const glm::vec3 rayDir(0.0f, -1.0f, 0.0f);
    const glm::vec3 upDir(0.0f, 1.0f, 0.0f);
    const float maxSlopeCos = std::cos(glm::radians(settings_.maxSlopeAngle));

    const glm::vec3 rayOrigin(worldCenter.x, settings_.sampleCeiling, worldCenter.z);
    const Assets::RayCastResult centerHit = bvh.RayCastInCPU(rayOrigin, rayDir);
    if (!centerHit.Hit)
    {
        return false;
    }

    const float groundY = rayOrigin.y - centerHit.T;
    outCell.groundHeight = groundY;

    const glm::vec3 normal(centerHit.Normal.x, centerHit.Normal.y, centerHit.Normal.z);
    if (glm::dot(normal, glm::vec3(0.0f, 1.0f, 0.0f)) < maxSlopeCos)
    {
        return false;
    }

    const glm::vec3 clearOrigin(worldCenter.x, groundY + 0.1f, worldCenter.z);
    const Assets::RayCastResult clearHit = bvh.RayCastInCPU(clearOrigin, upDir);
    if (clearHit.Hit && clearHit.T < settings_.clearanceHeight)
    {
        return false;
    }

    if (settings_.agentRadius > 0.001f)
    {
        const std::array<glm::vec2, 8> footprintOffsets = {
            glm::vec2(1.0f, 0.0f),
            glm::vec2(-1.0f, 0.0f),
            glm::vec2(0.0f, 1.0f),
            glm::vec2(0.0f, -1.0f),
            glm::vec2(kDiagonalInvSqrt, kDiagonalInvSqrt),
            glm::vec2(-kDiagonalInvSqrt, kDiagonalInvSqrt),
            glm::vec2(kDiagonalInvSqrt, -kDiagonalInvSqrt),
            glm::vec2(-kDiagonalInvSqrt, -kDiagonalInvSqrt)
        };

        for (const glm::vec2& unitOffset : footprintOffsets)
        {
            const glm::vec2 offset = unitOffset * settings_.agentRadius;
            const glm::vec3 sampleOrigin(worldCenter.x + offset.x, settings_.sampleCeiling, worldCenter.z + offset.y);
            const Assets::RayCastResult sampleHit = bvh.RayCastInCPU(sampleOrigin, rayDir);
            if (!sampleHit.Hit)
            {
                return false;
            }

            const float sampleGroundY = sampleOrigin.y - sampleHit.T;
            if (std::abs(sampleGroundY - groundY) > settings_.maxStepHeight)
            {
                return false;
            }

            const glm::vec3 sampleNormal(sampleHit.Normal.x, sampleHit.Normal.y, sampleHit.Normal.z);
            if (glm::dot(sampleNormal, glm::vec3(0.0f, 1.0f, 0.0f)) < maxSlopeCos)
            {
                return false;
            }

            const glm::vec3 sampleClearOrigin(sampleOrigin.x, sampleGroundY + 0.1f, sampleOrigin.z);
            const Assets::RayCastResult sampleClearHit = bvh.RayCastInCPU(sampleClearOrigin, upDir);
            if (sampleClearHit.Hit && sampleClearHit.T < settings_.clearanceHeight)
            {
                return false;
            }
        }
    }

    outCell.baseWalkable = true;
    outCell.walkable = true;
    return true;
}

int FNavGrid::GetFootprintMarginCells() const
{
    if (settings_.agentRadius <= 0.001f || settings_.cellSize <= 0.001f)
    {
        return 0;
    }
    return static_cast<int>(std::ceil(settings_.agentRadius / settings_.cellSize));
}

int FNavGrid::GetExtraErosionCells() const
{
    return std::max(0, GetFootprintMarginCells() - 1);
}
}
