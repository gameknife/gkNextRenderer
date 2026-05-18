#pragma once

#include "Common/CoreMinimal.hpp"
#include "Voyage3DDataLoader.hpp"

#include <glm/glm.hpp>

namespace Assets
{
    class Node;
}

namespace Voyage3D
{
    struct FShipRuntime
    {
        FShipDef def;
        int currentHp = 0;
        std::map<std::string, int> cargo;
        int cargoUsed = 0;
        glm::vec3 worldPos = glm::vec3(0.0f);
        glm::vec3 previousWorldPos = glm::vec3(0.0f);
        float yaw = 0.0f;
        float currentSpeed = 0.0f;
        float leftBroadsideCooldownMs = 0.0f;
        float rightBroadsideCooldownMs = 0.0f;
        float aiFireCooldownMs = 0.0f;
        bool active = true;
        bool enemy = false;
        std::shared_ptr<Assets::Node> rootNode;
        std::shared_ptr<Assets::Node> hullNode;
        std::shared_ptr<Assets::Node> mastNode;
        std::shared_ptr<Assets::Node> sailNode;
    };
}
