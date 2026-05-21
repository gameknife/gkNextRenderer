#pragma once

#include "Brotato3DPcgConfig.hpp"
#include "Brotato3DPcgTypes.hpp"
#include "Engine/Assets/Data/Vertex.hpp"

namespace Brotato3D::Pcg
{
    struct FTerrainMeshBuffers
    {
        std::vector<::Assets::Vertex> vertices;
        std::vector<uint32_t> indices;
    };

    bool BuildMapGraph(const FArenaPcgConfig& config,
                       const glm::vec2& halfExtent,
                       uint32_t sessionSeed,
                       FMapGraph& outGraph);

    FTerrainMeshBuffers BuildTerrainMesh(const FArenaPcgConfig& config, FMapGraph& graph);

    float SampleVisualHeight(uint32_t rootSeed,
                             float vertexJitterAmplitude,
                             float vertexJitterFrequency,
                             const glm::vec2& xz);
}
