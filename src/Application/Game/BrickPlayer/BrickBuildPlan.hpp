#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

namespace BrickPlayer
{
    struct FBrickBuildPart
    {
        std::string id;
        std::string partId;
        int color = 16;
        glm::vec3 position{0.0f};
        glm::vec3 rotationDegrees{0.0f};
    };

    struct FBrickBuildConnection
    {
        std::string partA;
        std::string connectorA;
        std::string partB;
        std::string connectorB;
    };

    struct FBrickBuildPlan
    {
        int version = 1;
        std::vector<FBrickBuildPart> parts;
        std::vector<FBrickBuildConnection> connections;
    };

    struct FBrickBuildConstraints
    {
        std::unordered_set<std::string> availablePartIds;
        std::unordered_map<std::string, int> inventory;
        std::function<bool(const FBrickBuildPart&, const FBrickBuildPart&)> overlaps;
        std::function<bool(const FBrickBuildConnection&)> connectionExists;
    };

    struct FBrickBuildValidation
    {
        bool valid = false;
        std::vector<std::string> errors;
    };

    FBrickBuildPlan ParseBrickBuildPlan(const nlohmann::json& json);
    FBrickBuildValidation ValidateBrickBuildPlan(const FBrickBuildPlan& plan,
                                                 const FBrickBuildConstraints& constraints);
    std::string_view BrickBuildPlanJsonSchema();
}
