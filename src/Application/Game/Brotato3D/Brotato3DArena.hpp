#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Model.hpp"

namespace Assets
{
    class Node;
    struct FMaterial;
}

namespace Brotato3D
{
    struct FArenaDef;

    struct FArenaResources
    {
        struct FBoxCollider
        {
            glm::vec3 center = glm::vec3(0.0f);
            glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            glm::vec3 extent = glm::vec3(0.0f);
        };

        struct FTerrainHeightSampler
        {
            bool enabled = false;
            uint32_t rootSeed = 0;
            float vertexJitterAmplitude = 0.0f;
            float vertexJitterFrequency = 0.0f;
        };

        std::vector<std::shared_ptr<Assets::Node>> groundNodes;
        std::vector<std::shared_ptr<Assets::Node>> borderNodes;
        std::vector<std::shared_ptr<Assets::Node>> propNodes;
        std::vector<FBoxCollider> propColliders;
        FTerrainHeightSampler terrainHeight;
        std::map<std::string, uint32_t> groundMaterialIds;
        std::map<std::string, uint32_t> borderMaterialIds;
    };

    void BuildArena(std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials,
                    std::vector<std::shared_ptr<Assets::Node>>& nodes,
                    FArenaResources& outResources,
                    const std::vector<FArenaDef>& arenaDefs,
                    const std::string& selectedArenaId);
}
