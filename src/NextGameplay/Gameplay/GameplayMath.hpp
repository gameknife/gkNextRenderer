#pragma once

#include "Common/CoreMinimal.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

namespace NextGameplay
{
    inline glm::vec3 NormalizeHorizontalOrZero(const glm::vec3& value)
    {
        const glm::vec3 horizontal(value.x, 0.0f, value.z);
        const float length = glm::length(horizontal);
        if (length <= 0.001f)
        {
            return glm::vec3(0.0f);
        }

        return horizontal / length;
    }

    inline float AdvanceYawToward(float currentYaw, const glm::vec3& desiredDirection, float turnSpeed,
                                  float deltaSeconds)
    {
        const glm::vec3 horizontalDir = NormalizeHorizontalOrZero(desiredDirection);
        if (glm::length(horizontalDir) <= 0.001f)
        {
            return currentYaw;
        }

        const float targetYaw = std::atan2(horizontalDir.x, horizontalDir.z);
        const float yawDelta = std::remainder(targetYaw - currentYaw, glm::two_pi<float>());
        const float maxStep = turnSpeed * deltaSeconds;
        if (std::abs(yawDelta) <= maxStep)
        {
            return targetYaw;
        }

        return currentYaw + glm::sign(yawDelta) * maxStep;
    }
}
