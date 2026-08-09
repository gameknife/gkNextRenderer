#pragma once

#include "Engine/Assets/GPU/UniformBuffer.hpp"

#include <algorithm>
namespace Assets::LightObjects
{
    inline LightObject Transform(const LightObject& localLight, const glm::mat4& worldTransform)
    {
        LightObject worldLight = localLight;
        if (localLight.lightType == LightTypePoint)
        {
            worldLight.p0 = worldTransform * glm::vec4(glm::vec3(localLight.p0), 1.0f);
            const float scale = std::max({
                glm::length(glm::vec3(worldTransform[0])),
                glm::length(glm::vec3(worldTransform[1])),
                glm::length(glm::vec3(worldTransform[2])),
            });
            worldLight.p0.w = localLight.p0.w * scale;
            return worldLight;
        }

        if (localLight.lightType == LightTypeArea)
        {
            worldLight.p0 = worldTransform * glm::vec4(glm::vec3(localLight.p0), 1.0f);
            worldLight.p1 = worldTransform * glm::vec4(glm::vec3(localLight.p1), 1.0f);
            worldLight.p3 = worldTransform * glm::vec4(glm::vec3(localLight.p3), 1.0f);
            const glm::vec3 crossEdges =
                glm::cross(glm::vec3(worldLight.p1 - worldLight.p0), glm::vec3(worldLight.p3 - worldLight.p0));
            const float area = glm::length(crossEdges);
            const glm::vec3 localCrossEdges =
                glm::cross(glm::vec3(localLight.p1 - localLight.p0), glm::vec3(localLight.p3 - localLight.p0));
            const float localGeometricArea = glm::length(localCrossEdges);
            const float measureScale =
                localGeometricArea > 1.0e-8f ? std::max(localLight.normal_area.w, 0.0f) / localGeometricArea : 0.0f;
            const float normalSign = glm::dot(localCrossEdges, glm::vec3(localLight.normal_area)) < 0.0f
                ? -1.0f
                : 1.0f;
            worldLight.normal_area = area > 1.0e-8f
                ? glm::vec4(crossEdges * (normalSign / area), area * measureScale)
                : glm::vec4(0.0f);
        }
        return worldLight;
    }
}
