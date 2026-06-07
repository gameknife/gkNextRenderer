#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace Brotato3D::Pcg
{
    enum class EBorderFracturePattern : uint8_t
    {
        Uniform = 0,
        Cluster = 1,
        Spike = 2,
    };

    struct FPropDef
    {
        std::string id;
        glm::vec2 footprintXZ = glm::vec2(0.8f);
        float visualHeight = 1.0f;
        float colliderHeight = 0.6f;
        glm::vec3 baseColor = glm::vec3(0.55f);
        float weight = 1.0f;
    };

    struct FArenaPcgConfig
    {
        bool enabled = false;
        uint32_t seedOverride = 0;
        int targetCells = 256;
        int lloydRelaxIterations = 2;
        float vertexJitterAmplitude = 0.48f;
        float vertexJitterFrequency = 0.18f;
        float borderHeight = 4.0f;
        int borderSegments = 48;
        EBorderFracturePattern borderPattern = EBorderFracturePattern::Cluster;
        float borderHeightJitter = 0.6f;
        float propPoissonRadius = 1.8f;
        int propPoissonMaxAttempts = 30;
        float spawnSafeRadius = 4.0f;
        float edgeKeepout = 1.2f;
        std::vector<FPropDef> props;
        std::vector<glm::vec3> palette;
    };
}
