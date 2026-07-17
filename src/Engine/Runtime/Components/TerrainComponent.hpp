#pragma once
#include "Engine/Assets/Core/Component.hpp"
#include "Engine/Runtime/Reflection/ReflectionMacros.hpp"

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

namespace Runtime
{
    // Semantic heightfield attached by terrain-producing loaders (SCAD
    // gk_terrain). Physics and navigation keep working off the scene mesh;
    // this component answers fast gameplay queries — ground snapping, spawn
    // filtering, wading checks — from the exact same triangulated grid the
    // mesh was built from, so all three views of the ground agree.
    //
    // Data lives in the producer's local frame (x/y horizontal plane, z up);
    // localToWorld maps local (x, y, height) into engine world space. Queries
    // assume the terrain has not been tilted (local up stays world up).
    class TerrainComponent : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(TerrainComponent)

        struct FGridData
        {
            int cellsX = 0;
            int cellsY = 0;
            double sizeX = 0.0; // local domain extents, centered on the origin
            double sizeY = 0.0;
            uint64_t seed = 0;

            // Vertex grid (cellsX+1)*(cellsY+1), row-major, jittered XY + height.
            std::vector<glm::dvec3> verts;
            // Per cell, row-major.
            std::vector<uint8_t> diagFlip; // 1 => cell splits along b-d
            std::vector<uint8_t> biome;
            std::vector<uint8_t> flags; // kFlagWater / kFlagRoad / kFlagPad
            std::vector<double> waterLevel;

            glm::dmat4 localToWorld{1.0};
            glm::dmat4 worldToLocal{1.0};
        };

        static constexpr uint8_t kFlagWater = 1u << 0;
        static constexpr uint8_t kFlagRoad = 1u << 1;
        static constexpr uint8_t kFlagPad = 1u << 2;

        TerrainComponent() = default;

        void SetGridData(std::shared_ptr<const FGridData> data) { data_ = std::move(data); }
        const std::shared_ptr<const FGridData>& GetGridData() const { return data_; }
        bool HasData() const { return data_ != nullptr && !data_->verts.empty(); }

        // All queries take engine world-space coordinates (XZ ground plane).
        float SampleHeight(float worldX, float worldZ) const; // world Y of the surface
        glm::vec3 SampleNormal(float worldX, float worldZ) const;
        float SampleSlopeDegrees(float worldX, float worldZ) const;
        bool IsWater(float worldX, float worldZ) const;
        float WaterSurface(float worldX, float worldZ) const; // world Y; only valid when IsWater
        bool IsWalkable(float worldX, float worldZ, float maxSlopeDeg = 45.0f) const;
        uint8_t BiomeId(float worldX, float worldZ) const;

        // Read-only reflection info.
        int GetCellsX() const { return data_ ? data_->cellsX : 0; }
        int GetCellsY() const { return data_ ? data_->cellsY : 0; }
        float GetSizeX() const { return data_ ? static_cast<float>(data_->sizeX) : 0.0f; }
        float GetSizeY() const { return data_ ? static_cast<float>(data_->sizeY) : 0.0f; }
        int GetSeed() const { return data_ ? static_cast<int>(data_->seed) : 0; }

    private:
        // Maps world XZ into the local horizontal plane; false without data.
        bool WorldToLocal(float worldX, float worldZ, glm::dvec2& outLocal) const;
        bool LocateTriangle(const glm::dvec2& local, glm::dvec3& a, glm::dvec3& b, glm::dvec3& c) const;
        int CellIndexAt(const glm::dvec2& local) const;

        std::shared_ptr<const FGridData> data_;
    };
} // namespace Runtime
