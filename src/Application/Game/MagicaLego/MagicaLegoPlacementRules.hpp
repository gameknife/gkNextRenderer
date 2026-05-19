#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include <glm/ext/vector_int2_sized.hpp>
#include <glm/ext/vector_int3_sized.hpp>

enum class EOrientation : uint8_t;

namespace MagicaLego::Placement
{
    struct FBlockRule
    {
        bool isThin = false;
        bool topOnlyTarget = false;
        std::vector<glm::i16vec2> footprint;
    };

    const FBlockRule& GetBlockRule(std::string_view typeName);
    bool IsThinBlockType(std::string_view typeName);
    std::vector<glm::i16vec3> BuildOccupiedCells(std::string_view typeName, const glm::i16vec3& anchor, EOrientation orientation);
}
