#pragma once

#include "Utilities/Glm.hpp"
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <map>

namespace Assets
{
    struct Joint
    {
        std::string Name;
        int32_t ParentIndex = -1;
        glm::mat4 InverseBindMatrix = glm::mat4(1.0f);
        
        // Local transform in bind pose (optional, useful for debugging or resetting)
        glm::vec3 Translation = glm::vec3(0.0f);
        glm::quat Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        glm::vec3 Scale = glm::vec3(1.0f);

        std::vector<int32_t> Children;
    };

    struct Skeleton
    {
        std::string Name;
        std::vector<Joint> Joints;
        int32_t RootJointIndex = -1;
    };

    // Forward declaration of AnimationTrack if needed here, 
    // or we can reuse/extend the existing AnimationTrack in Model.hpp/Animation.hpp
    // For now, we will rely on the existing AnimationTrack structure in Model.hpp
    // but we might need to organize them better.
}
