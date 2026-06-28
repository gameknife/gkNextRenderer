#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include <glm/glm.hpp>

struct ImVec2;

namespace Runtime::EngineHelper
{
    bool TryProjectWorldToScreen(const glm::vec3& worldPos, ImVec2& outImGuiPos);
    bool TryProjectWorldToScreenWithCamera(const Assets::Camera& camera, const glm::vec3& worldPos, ImVec2& outImGuiPos);
    bool TryProjectWorldToScreenForGame(const NextGameInstanceBase& gameInstance, const glm::vec3& worldPos, ImVec2& outImGuiPos);
    void GetScreenToWorldRay(glm::vec2 locationSS, glm::vec3& origin, glm::vec3& dir);
    void GetScreenToWorldRayWithCamera(const Assets::Camera& camera, glm::vec2 locationSS, glm::vec2 viewportPos,
                                       glm::vec2 viewportSize, glm::vec3& origin, glm::vec3& dir);
    void DrawAuxLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size = 1);
    void DrawAuxBox(glm::vec3 min, glm::vec3 max, glm::vec4 color, float size = 1);
    void DrawAuxPoint(glm::vec3 location, glm::vec4 color, float size = 1, int32_t durationInTick = 0);
}
