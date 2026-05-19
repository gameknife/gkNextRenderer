#include "Voyage3DWorldMap.hpp"

#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"

namespace Voyage3D::WorldMap
{
    glm::vec3 GeoToWorld(float lon, float lat)
    {
        constexpr float minLon = -6.0f;
        constexpr float maxLon = 36.0f;
        constexpr float minLat = 30.0f;
        constexpr float maxLat = 46.0f;
        constexpr float minX = -30.0f;
        constexpr float maxX = 85.0f;
        constexpr float minZ = -90.0f;
        constexpr float maxZ = 20.0f;

        const float x = minX + ((lon - minLon) / (maxLon - minLon)) * (maxX - minX);
        const float z = minZ + ((lat - minLat) / (maxLat - minLat)) * (maxZ - minZ);
        return glm::vec3(x, 0.0f, z);
    }

    void BuildOcean(std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials,
                    std::vector<std::shared_ptr<Assets::Node>>& nodes)
    {
        models.push_back(Assets::FProcModel::CreateBox(glm::vec3(-30.0f, -0.05f, -90.0f), glm::vec3(85.0f, 0.0f, 20.0f)));
        const uint32_t modelId = static_cast<uint32_t>(models.size() - 1);
        const uint32_t materialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.10f, 0.30f, 0.60f));
        nodes.push_back(SceneBuilder::CreateRenderNode("Voyage3D_Ocean",
                                                       glm::vec3(0.0f),
                                                       glm::vec3(1.0f),
                                                       static_cast<uint32_t>(nodes.size()),
                                                       modelId,
                                                       materialId,
                                                       true,
                                                       glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                                       false));
    }

    void BuildLandmass(const std::vector<FLandmassBlock>& blocks,
                       std::vector<Assets::Model>& models,
                       std::vector<Assets::FMaterial>& materials,
                       std::vector<std::shared_ptr<Assets::Node>>& nodes)
    {
        for (const FLandmassBlock& block : blocks)
        {
            models.push_back(Assets::FProcModel::CreateBox(block.min, block.max));
            const uint32_t modelId = static_cast<uint32_t>(models.size() - 1);
            const uint32_t materialId = SceneBuilder::AddLambertianMaterial(materials, block.color);
            nodes.push_back(SceneBuilder::CreateRenderNode("Voyage3D_Land_" + block.name,
                                                           glm::vec3(0.0f),
                                                           glm::vec3(1.0f),
                                                           static_cast<uint32_t>(nodes.size()),
                                                           modelId,
                                                           materialId));
        }
    }
}
