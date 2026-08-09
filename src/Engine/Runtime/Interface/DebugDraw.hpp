#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Runtime
{
    class IDebugDraw
    {
    public:
        virtual ~IDebugDraw() = default;
        virtual void AddLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size, bool depthTest) = 0;
        virtual void AddBox(glm::vec3 min, glm::vec3 max, glm::vec4 color, float size, bool depthTest) = 0;
        virtual void AddPoint(glm::vec3 location, glm::vec4 color, float size,
                              int32_t durationInTick, bool depthTest) = 0;
    };
}
