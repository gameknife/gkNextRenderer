#pragma once

#include "Common/CoreMinimal.hpp"
#include "Voyage3DDataLoader.hpp"

#include <glm/glm.hpp>

namespace Assets
{
    class Node;
    class Model;
    struct FMaterial;
    struct LightObject;
}

namespace Voyage3D::WorldMap
{
    glm::vec3 GeoToWorld(float lon, float lat);
    void BuildOcean(std::vector<Assets::Model>& models,
                    std::vector<Assets::FMaterial>& materials,
                    std::vector<std::shared_ptr<Assets::Node>>& nodes);
    void BuildLandmass(const std::vector<FLandmassBlock>& blocks,
                       std::vector<Assets::Model>& models,
                       std::vector<Assets::FMaterial>& materials,
                       std::vector<std::shared_ptr<Assets::Node>>& nodes);
}
