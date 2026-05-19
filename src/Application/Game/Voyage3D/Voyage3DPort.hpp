#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Voyage3DDataLoader.hpp"

#include <glm/glm.hpp>

namespace Assets
{
    class Node;
}

namespace Voyage3D
{
    struct FPortRuntime
    {
        FPortDef def;
        glm::vec3 worldPos = glm::vec3(0.0f);
        std::shared_ptr<Assets::Node> rootNode;
        std::shared_ptr<Assets::Node> towerNode;
        std::shared_ptr<Assets::Node> roofNode;
        std::shared_ptr<Assets::Node> anchorNode;
        std::map<std::string, int> currentPrices;
        std::map<std::string, int> stock;
        std::map<std::string, float> nextVisitDiscountFactor;
        bool visited = false;
    };
}
