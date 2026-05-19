#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace Brotato3D::Pcg
{
    struct FVoronoiSite
    {
        glm::vec2 positionXZ = glm::vec2(0.0f);
        int paletteIndex = 0;
    };

    struct FVoronoiCell
    {
        int siteIndex = -1;
        std::vector<glm::vec2> polygonXZ;
        glm::vec2 centroidXZ = glm::vec2(0.0f);
        std::vector<int> neighborCellIds;
    };

    struct FCellVertexDisplacement
    {
        glm::vec2 xz = glm::vec2(0.0f);
        float yVisual = 0.0f;
    };

    struct FPropPlacement
    {
        std::string id;
        glm::vec2 positionXZ = glm::vec2(0.0f);
        float rotationYRadians = 0.0f;
    };

    struct FBorderSegment
    {
        glm::vec2 baseStartXZ = glm::vec2(0.0f);
        glm::vec2 baseEndXZ = glm::vec2(0.0f);
        float topY = 0.0f;
        float thickness = 0.2f;
        glm::vec3 color = glm::vec3(1.0f);
    };

    struct FMapGraph
    {
        glm::vec2 arenaHalfExtent = glm::vec2(0.0f);
        uint32_t rootSeed = 0;
        std::vector<FVoronoiSite> sites;
        std::vector<FVoronoiCell> cells;
        std::vector<FCellVertexDisplacement> vertexBuffer;
        std::vector<uint32_t> indexBuffer;
        std::vector<uint32_t> sectionMaterialOffsets;
        std::vector<FPropPlacement> props;
        std::vector<FBorderSegment> borderSegments;
    };
}
