#pragma once

#include "Common/CoreMinimal.hpp"
#include <glm/glm.hpp>

class NextEngine;

namespace NextEngineHelper
{
    glm::vec3 ProjectScreenToWorld(glm::vec2 locationSS);
    glm::vec3 ProjectWorldToScreen(glm::vec3 locationWS);
    void GetScreenToWorldRay(glm::vec2 locationSS, glm::vec3& origin, glm::vec3& dir);
    void DrawAuxLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size = 1);
    void DrawAuxBox(glm::vec3 min, glm::vec3 max, glm::vec4 color, float size = 1);
    void DrawAuxPoint(glm::vec3 location, glm::vec4 color, float size = 1, int32_t durationInTick = 0);
}
