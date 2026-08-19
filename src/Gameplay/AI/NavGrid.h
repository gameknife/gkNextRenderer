#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <functional>
#include <glm/glm.hpp>
#include <vector>

namespace Assets::CPU
{
    class FCPUAccelerationStructure;
}

namespace NextGameplay
{
    struct FNavCell
    {
        float groundHeight = 0.0f;
        bool baseWalkable = false;
        bool walkable = false;
    };

    struct FNavGridSettings
    {
        float cellSize = 0.75f;
        float agentRadius = 0.0f;
        float maxSlopeAngle = 50.0f;
        float clearanceHeight = 2.2f;
        float maxStepHeight = 0.5f;
        float sampleCeiling = 50.0f;
        float floorHeightTolerance = 1.5f;
        glm::vec3 worldMin{0.0f};
        glm::vec3 worldMax{0.0f};
    };

    class FNavGrid
    {
    public:
        void Build(Assets::CPU::FCPUAccelerationStructure& bvh, const FNavGridSettings& settings);
        void RebuildDirtyRegion(Assets::CPU::FCPUAccelerationStructure& bvh, const glm::vec3& dirtyWorldMin, const glm::vec3& dirtyWorldMax);

        // Marks cells whose predicate returns true as unwalkable — semantic
        // vetoes the geometry raycast cannot see (e.g. river water: rays hit
        // the dry bed below the raycast-invisible water surface). Predicate
        // gets the cell world position with the sampled ground height, so a
        // bridge deck above the water line stays walkable. Call after Build();
        // RebuildDirtyRegion() re-samples geometry only, so re-apply after it.
        void MaskUnwalkable(const std::function<bool(const glm::vec3&)>& predicate);

        std::vector<glm::vec3> FindPath(const glm::vec3& from, const glm::vec3& to, float referenceHeight) const;

        bool IsWalkable(const glm::vec3& worldPos) const;
        // Ground height under worldPos, as sampled when the grid was built.
        // Returns false outside the grid or on a cell no down-ray ever hit, so
        // the caller can fall back rather than silently teleport to y = 0.
        // Unlike the terrain height field this follows what an agent actually
        // stands on — a road deck, a pier, a bridge — not just the heightmap.
        bool SampleGroundHeight(const glm::vec3& worldPos, float& outHeight) const;
        bool IsBuilt() const { return !cells_.empty(); }
        int GetWidth() const { return width_; }
        int GetHeight() const { return height_; }
        glm::vec3 GetWorldMin() const { return settings_.worldMin; }
        float GetCellSize() const { return settings_.cellSize; }

        bool IsCellWalkable(int gx, int gz) const;
        bool IsCellReachable(int gx, int gz, float referenceHeight) const;
        glm::vec3 GetCellWorldPosition(int gx, int gz) const;
        std::vector<uint8_t> BuildReachabilityMask(const glm::vec3& from, float referenceHeight) const;

    private:
        struct FGridRect
        {
            int minGx = 0;
            int minGz = 0;
            int maxGx = -1;
            int maxGz = -1;
        };

        glm::ivec2 WorldToGrid(const glm::vec3& worldPos) const;
        glm::vec3 GridToWorld(int gx, int gz) const;
        int CellIndex(int gx, int gz) const;
        bool InBounds(int gx, int gz) const;
        glm::ivec2 FindNearestWalkable(const glm::ivec2& cell, float referenceHeight) const;
        void SmoothPath(std::vector<glm::ivec2>& gridPath) const;
        bool GridLineWalkable(const glm::ivec2& a, const glm::ivec2& b) const;
        bool CanTraverse(int fromGx, int fromGz, int toGx, int toGz) const;
        bool CanTraverseByIndex(int fromIdx, int toIdx) const;
        FGridRect MakeGridRectFromWorldBounds(const glm::vec3& worldMin, const glm::vec3& worldMax) const;
        FGridRect ExpandGridRect(const FGridRect& rect, int margin) const;
        bool IsGridRectValid(const FGridRect& rect) const;
        void SampleBaseWalkability(Assets::CPU::FCPUAccelerationStructure& bvh, const FGridRect& rect);
        void UpdateWalkabilityFromBase(const FGridRect& rect);
        bool SampleBaseCell(Assets::CPU::FCPUAccelerationStructure& bvh, int gx, int gz, FNavCell& outCell) const;
        int GetFootprintMarginCells() const;
        int GetExtraErosionCells() const;

        std::vector<FNavCell> cells_;
        FNavGridSettings settings_;
        int width_ = 0;
        int height_ = 0;
    };
}
