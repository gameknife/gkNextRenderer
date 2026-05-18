#include "Common/CoreMinimal.hpp"
#include "MagicaLegoPlacementRules.hpp"
#include "MagicaLegoConstants.hpp"
#include "MagicaLegoGameInstance.hpp"

namespace MagicaLego::Placement
{
    namespace
    {
        const FBlockRule DefaultRule{
            .isThin = false,
            .topOnlyTarget = false,
            .footprint = {{0, 0}}
        };

        const FBlockRule Thin1x1Rule{
            .isThin = true,
            .topOnlyTarget = true,
            .footprint = {{0, 0}}
        };

        // Matches current model indicator extents: X = [-1, 0], Z = [0, 1].
        const FBlockRule Plate2x2Rule{
            .isThin = true,
            .topOnlyTarget = true,
            .footprint = {{-1, 0}, {0, 0}, {-1, 1}, {0, 1}}
        };

        // L-shape in a 2x2 area. Rotated by orientation.
        const FBlockRule Corner2x2Rule{
            .isThin = false,
            .topOnlyTarget = false,
            .footprint = {{0, 0}, {1, 0}, {0, 1}}
        };

        // Simplified as a full-height 2-cell block along local +Z.
        const FBlockRule Slope1x2Rule{
            .isThin = false,
            .topOnlyTarget = false,
            .footprint = {{0, 0}, {0, 1}}
        };

        glm::i16vec2 RotateFootprintOffset(const glm::i16vec2& offset, EOrientation orientation)
        {
            using namespace MagicaLego::Grid;

            glm::vec4 localPos(
                static_cast<float>(offset.x) * UnitX,
                0.0f,
                static_cast<float>(offset.y) * UnitZ,
                1.0f);
            glm::vec4 rotated = GetOrientationMatrix(orientation) * localPos;

            return {
                static_cast<int16_t>(std::round(rotated.x / UnitX)),
                static_cast<int16_t>(std::round(rotated.z / UnitZ))
            };
        }
    }

    const FBlockRule& GetBlockRule(std::string_view typeName)
    {
        if (typeName == "Flat1x1" || typeName == "Plate1x1" || typeName == "Button1x1")
        {
            return Thin1x1Rule;
        }
        if (typeName == "Plate2x2")
        {
            return Plate2x2Rule;
        }
        if (typeName == "Corner2x2")
        {
            return Corner2x2Rule;
        }
        if (typeName == "Slope1x2")
        {
            return Slope1x2Rule;
        }

        return DefaultRule;
    }

    bool IsThinBlockType(std::string_view typeName)
    {
        return GetBlockRule(typeName).isThin;
    }

    std::vector<glm::i16vec3> BuildOccupiedCells(std::string_view typeName, const glm::i16vec3& anchor, EOrientation orientation)
    {
        const FBlockRule& rule = GetBlockRule(typeName);
        std::vector<glm::i16vec3> occupiedCells;
        occupiedCells.reserve(rule.footprint.size());

        for (const glm::i16vec2& footprintOffset : rule.footprint)
        {
            glm::i16vec2 rotatedOffset = RotateFootprintOffset(footprintOffset, orientation);
            occupiedCells.push_back(glm::i16vec3(
                static_cast<int16_t>(anchor.x + rotatedOffset.x),
                anchor.y,
                static_cast<int16_t>(anchor.z + rotatedOffset.y)));
        }

        return occupiedCells;
    }
}
