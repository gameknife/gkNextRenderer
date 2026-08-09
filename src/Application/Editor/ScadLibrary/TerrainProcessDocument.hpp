#pragma once

#include "Modules/ScadLoader/FScadTerrain.h"
#include "Modules/ScadLoader/FScadTypes.h"

#include <cstddef>
#include <map>
#include <string>
#include <vector>

namespace ScadLibrary
{
    enum class ETerrainProcessRuleType
    {
        HeightAnchor,
        Place,
        PlaceTilt,
        Snap,
        Along,
        Scatter,
    };

    struct FTerrainProcessRule
    {
        ETerrainProcessRuleType type = ETerrainProcessRuleType::Place;
        double x = 0.0;
        double y = 0.0;
        double dz = 0.0;

        // translate([x, y, gk_terrain_height(t, sampleX, sampleY) + dz])
        // keeps bridge placement separate from its bank-height anchor.
        double sampleX = 0.0;
        double sampleY = 0.0;

        // ter_place_tilt
        double maxTilt = 12.0;
        double probe = 0.8;

        // ter_snap / ter_along
        std::vector<glm::dvec2> points;
        double step = 6.0;
        int seed = 0;
        double offset = 0.0;

        // ter_scatter
        int count = 10;
        glm::dvec4 region{-20.0, -20.0, 20.0, 20.0};
        bool circularRegion = true;
        glm::dvec2 regionCenter{0.0, 0.0};
        double regionRadius = 20.0;
        double minHeight = -1.0e9;
        double maxHeight = 1.0e9;
        double maxSlope = 90.0;
        double avoidWater = 0.0;
        std::vector<std::string> biomes;
        bool randomRotation = true;
        int variants = 0;
        glm::dvec2 scaleRange{1.0, 1.0};

        // The child statement is intentionally kept as SCAD. It can contain
        // rotate/translate chains, lay_pick, module arguments, or a block.
        std::string childSource;

        bool removed = false;
        size_t sourceBegin = std::string::npos;
        size_t sourceEnd = std::string::npos;
    };

    // Structured editor model for the engine-specific terrain dialect. It
    // rewrites only the TERR assignment and recognized top-level ter_* calls;
    // all other hand-written SCAD and comments stay byte-for-byte intact.
    class FTerrainProcessDocument
    {
    public:
        bool Parse(const std::string& source, const Assets::Scad::Scope& topLevel,
                   const std::map<std::string, Assets::Scad::Value>& topLevelVariables, std::string& outError,
                   std::vector<std::string>& outWarnings);

        Assets::Scad::FTerrainSpec& Terrain() { return terrain_; }
        const Assets::Scad::FTerrainSpec& Terrain() const { return terrain_; }
        std::vector<FTerrainProcessRule>& Rules() { return rules_; }
        const std::vector<FTerrainProcessRule>& Rules() const { return rules_; }
        const std::string& TerrainVariable() const { return terrainVariable_; }

        FTerrainProcessRule& AddRule(ETerrainProcessRuleType type, std::string childSource = {});
        void DuplicateRule(size_t index, bool offsetPosition = true);
        void RemoveRule(size_t index);
        size_t ActiveRuleCount() const;

        std::string BuildSource() const;

        static const char* FeatureTypeName(Assets::Scad::FTerrainFeature::EType type);
        static const char* RuleTypeName(ETerrainProcessRuleType type);

    private:
        std::string source_;
        std::string terrainVariable_ = "TERR";
        Assets::Scad::FTerrainSpec terrain_;
        std::vector<FTerrainProcessRule> rules_;
        size_t terrainAssignmentBegin_ = std::string::npos;
        size_t terrainAssignmentEnd_ = std::string::npos;
        size_t insertionPoint_ = std::string::npos;
    };
} // namespace ScadLibrary
