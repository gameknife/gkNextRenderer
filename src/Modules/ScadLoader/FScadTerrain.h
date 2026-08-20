#pragma once

// ============================================================================
// FScadTerrain.h - Native low-poly heightfield terrain for the SCAD loader.
//
// Exposed to the SCAD language as engine extensions (gk_ prefix, not part of
// OpenSCAD):
//   * module   gk_terrain(TERR)             -> terrain + water geometry
//   * function gk_terrain_height(TERR, x,y) -> surface height (mesh-exact)
//   * function gk_terrain_info(TERR, x,y)   -> [height, slopeDeg, water, biome]
//
// TERR is a nested list literal, versioned with a "gkterr1" tag:
//   ["gkterr1", [sizeX,sizeY], [cellsX,cellsY], seed,
//    [baseHeight, relief, roughness], waterLevel|undef, "palette",
//    [ feature, ... ]]
// Features (applied in order, each an ordered operator on the heightfield):
//   ["mountain", [x,y], radius, height, rugged]
//   ["ridge",    [[x,y],...], width, height]
//   ["plateau",  [x,y], radius, height]
//   ["lake",     [x,y], radius, depth]
//   ["river",    [[x,y],...], width, depth]     // carve + water surface
//   ["road",     [[x,y],...], width]            // flatten + dirt color
//   ["pad",      [x,y], [w,d], rotDeg]          // flat build site
//   ["hmap",     path|[cols,rows][,grid], mode, zScale, zBias]  // sampled DEM
//
// The hmap operator stamps a sampled heightfield (real-world elevation, a
// sculpted mask, ...) into the field. Two source forms:
//   ["hmap", "assets/geo/<tile>/terrain.hmap", "set", 1, 0]
//   ["hmap", [cols, rows], [h00, h01, ...], "set", 1, 0]
// mode is "set" (replace) or "add". The side-car file is a little-endian blob:
//   "GKHM", u32 version=1, u32 cols, u32 rows, f32 originX, originY,
//   f32 cellX, cellY, f32 scale, bias, then int16[cols*rows] row-major
//   (x fastest, +y with row index); height = raw * scale + bias.
// Paths are runtime-root relative and read through the package file system, so
// they behave identically in packed and loose builds.
//
// All geometry is produced in SCAD space (Z-up, centered on the origin) and
// deterministic for a given spec (integer hashing only, no libc rand). The
// triangulated grid is the single source of truth: height queries interpolate
// the same jittered triangles that are rendered and collided against.
// ============================================================================

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Modules/ScadLoader/FScadGeometry.h"
#include "Modules/ScadLoader/FScadTypes.h"

namespace Assets::Scad
{
    // Stable biome ids (returned by gk_terrain_info and stored per cell).
    enum class ETerrainBiome : uint8_t
    {
        Grass = 0,
        GrassDark = 1,
        DryGrass = 2,
        Sand = 3,
        Rock = 4,
        RockHigh = 5,
        Snow = 6,
        Bed = 7,  // river/lake bed (under water)
        Road = 8,
        Pad = 9,
        Count
    };

    // Sampled heightfield backing an ["hmap", ...] feature. Either decoded from
    // a .hmap side-car asset or built from an inline TERR literal. Immutable and
    // shared: identical sources resolve to one instance (see HeightGridCacheKey).
    struct FHeightGrid
    {
        int cols = 0;
        int rows = 0;
        glm::dvec2 origin{0.0, 0.0}; // SCAD-space position of sample (0, 0)
        glm::dvec2 cell{1.0, 1.0};   // sample spacing (meters)
        std::vector<float> values;   // row-major, x fastest, +y with row index
        uint64_t contentHash = 0;

        // Bilinear sample; xy outside the grid clamps to the border value.
        double Sample(double x, double y) const;
    };

    struct FTerrainFeature
    {
        enum class EType : uint8_t
        {
            Mountain,
            Ridge,
            Plateau,
            Lake,
            River,
            Road,
            Pad,
            Hmap
        };

        EType type = EType::Mountain;
        glm::dvec2 at{0.0, 0.0};   // mountain/plateau/lake/pad
        glm::dvec2 size{0.0, 0.0}; // pad [w, d]
        double rot = 0.0;          // pad rotation (degrees, around +Z)
        double radius = 0.0;       // mountain/plateau/lake
        double height = 0.0;       // mountain/ridge/plateau
        double depth = 0.0;        // lake/river
        double width = 0.0;        // ridge/river/road
        double rugged = 0.0;       // mountain noise perturbation [0..1]
        std::vector<glm::dvec2> pts; // ridge/river/road polyline

        // hmap
        std::string path;   // runtime-root-relative .hmap ("" => inline literal)
        bool hmapAdd = false; // "add" mode; default "set" replaces the height
        double zScale = 1.0;
        double zBias = 0.0;
        std::shared_ptr<const FHeightGrid> grid;
    };

    struct FTerrainSpec
    {
        glm::dvec2 size{100.0, 100.0};
        glm::ivec2 cells{50, 50};
        uint64_t seed = 0;
        double baseHeight = 0.0;
        double relief = 1.0;    // fbm amplitude (0 = perfectly flat base)
        double roughness = 0.5; // 0..1, mapped to fbm frequency/octaves
        bool hasWaterLevel = false;
        double waterLevel = 0.0; // global sea level (when hasWaterLevel)
        std::string palette = "temperate";
        std::vector<FTerrainFeature> features;
    };

    // Compiled + triangulated terrain. Built once per unique spec and cached by
    // the evaluator; rendering, height queries and (later) the engine-side
    // TerrainComponent all read this same data.
    struct FTerrainData
    {
        FTerrainSpec spec;

        // Vertex grid: (cells.x+1) * (cells.y+1), row-major (x fastest).
        // XY deterministically jittered (borders / road / pad kept straight).
        int vertsX = 0;
        int vertsY = 0;
        std::vector<glm::dvec3> verts;

        // Per cell (cells.x * cells.y, row-major).
        std::vector<uint8_t> cellDiagFlip; // 1 => diagonal b-d instead of a-c
        std::vector<uint8_t> cellBiome;    // ETerrainBiome
        std::vector<uint8_t> cellFlags;    // bit0 water, bit1 road, bit2 pad
        std::vector<double> cellWater;     // water surface z (valid when bit0)

        double minHeight = 0.0;
        double maxHeight = 0.0;

        struct ColoredTris
        {
            glm::vec4 color{0.5f, 0.5f, 0.5f, 1.0f};
            std::string materialName;
            TriSoup tris; // SCAD space (Z-up)
        };
        std::vector<ColoredTris> landGeom;  // land + skirt + bottom, per color
        std::vector<ColoredTris> waterGeom; // translucent water surfaces

        static constexpr uint8_t kFlagWater = 1u << 0;
        static constexpr uint8_t kFlagRoad = 1u << 1;
        static constexpr uint8_t kFlagPad = 1u << 2;

        // ---- Queries (SCAD local space; xy clamped to the terrain domain) ----
        double HeightAt(double x, double y) const;
        glm::dvec3 NormalAt(double x, double y) const; // unit, Z-up
        // h/slopeDeg from the containing triangle; water/biome from the cell.
        void InfoAt(double x, double y, double& outHeight, double& outSlopeDeg,
                    bool& outWater, uint8_t& outBiome) const;

        int CellIndexAt(double x, double y) const; // clamped; -1 only when empty

    private:
        // Locates the triangle containing (x,y); returns vertex indices.
        bool LocateTriangle(double x, double y, glm::dvec3& a, glm::dvec3& b, glm::dvec3& c) const;
    };

    class ScadTerrain
    {
    public:
        // Decodes a TERR value; returns false with a message on malformed input
        // (wrong tag, bad field shapes). Unknown feature types are skipped and
        // reported through outWarnings (evaluation still succeeds).
        static bool DecodeSpec(const Value& value, FTerrainSpec& outSpec,
                               std::string& outError, std::vector<std::string>& outWarnings);

        // Canonical byte-stable key of a decoded spec (evaluator cache key).
        static std::string SpecCacheKey(const FTerrainSpec& spec);

        // Compiles the heightfield and builds the triangulated mesh + masks.
        static std::shared_ptr<const FTerrainData> Build(const FTerrainSpec& spec);

        // Decodes a .hmap blob (see the format note above). Exposed for tests
        // and for tooling that wants to validate a generated file.
        static std::shared_ptr<const FHeightGrid> DecodeHeightGrid(
            const std::vector<uint8_t>& bytes, std::string& outError);
    };
} // namespace Assets::Scad
