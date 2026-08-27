#pragma once

#include <glm/glm.hpp>

#include <vector>

namespace Assets
{
    class Scene;
}
namespace Runtime
{
    class TerrainComponent;
}

namespace NextWorldTravel
{
    // Every terrain in the loaded scene, queried as one ground.
    //
    // A geo area of more than one part is a grid of TerrainComponents, each
    // 1 km across and centred on its own origin (see the geo design: a terrain
    // is capped at 176 cells, so covering 3 or 5 km means tiling). Every
    // TerrainComponent query clamps to its own domain, so asking the first one
    // about a point two parts away answers with its border height — the
    // character walks off the middle tile and starts skating along a wall of
    // clamped ground. This picks the terrain the point is actually on.
    //
    // The lookup is a linear scan. The grid is capped at 7x7, and the callers
    // are a handful of samples per frame plus one pass over the POI list.
    class FGeoTerrainSet
    {
    public:
        // Gathers every TerrainComponent that carries data. Safe to call again;
        // it replaces what it had.
        void Collect(Assets::Scene& scene);
        void Clear();

        bool HasData() const { return !terrains_.empty(); }
        int Count() const { return static_cast<int>(terrains_.size()); }

        // The part a query belongs to: the one containing the point, else the
        // nearest by centre. Null only when the set is empty.
        const Runtime::TerrainComponent* At(float worldX, float worldZ) const;

        // Primary is the part covering the world origin — the centre of the
        // area, the part the place was named for. Spawn selection stays inside
        // it rather than wandering to a corner two kilometres away.
        const Runtime::TerrainComponent* Primary() const { return primary_; }

        float SampleHeight(float worldX, float worldZ) const;
        bool IsWater(float worldX, float worldZ) const;

        // Extent of the primary part, which is what spawn search should cover.
        float GetSizeX() const;
        float GetSizeY() const;
        // Extent of the whole area, which is what a camera has to frame.
        float TotalSizeX() const { return worldMax_.x - worldMin_.x; }
        float TotalSizeY() const { return worldMax_.y - worldMin_.y; }
        glm::vec2 WorldMin() const { return worldMin_; }
        glm::vec2 WorldMax() const { return worldMax_; }

        int GetCellsX() const;
        int GetCellsY() const;

    private:
        std::vector<const Runtime::TerrainComponent*> terrains_;
        const Runtime::TerrainComponent* primary_ = nullptr;
        glm::vec2 worldMin_{0.0f, 0.0f};
        glm::vec2 worldMax_{0.0f, 0.0f};
    };
} // namespace NextWorldTravel
