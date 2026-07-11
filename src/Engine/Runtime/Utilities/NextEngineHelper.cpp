#include "Engine/Runtime/Utilities/NextEngineHelper.h"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <imgui.h>

namespace
{
    bool TryNdcToImGuiPos(const glm::vec3& ndc, ImVec2& outImGuiPos, bool invertY)
    {
        if (ndc.z < -1.0f || ndc.z > 1.0f || ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f)
        {
            return false;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport || viewport->Size.x <= 1.0f || viewport->Size.y <= 1.0f)
        {
            return false;
        }

        outImGuiPos.x = viewport->Pos.x + (ndc.x * 0.5f + 0.5f) * viewport->Size.x;
        const float yNdc = invertY ? -ndc.y : ndc.y;
        outImGuiPos.y = viewport->Pos.y + (yNdc * 0.5f + 0.5f) * viewport->Size.y;
        return true;
    }
}

namespace Runtime::EngineHelper
{
    bool TryProjectWorldToScreen(const glm::vec3& worldPos, ImVec2& outImGuiPos)
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return false;
        }

        const auto& ubo = engine->GetLastUniformBufferObject();
        const glm::vec4 clip = ubo.ViewProjection * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.001f)
        {
            return false;
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        if (ndc.z < 0.0f || ndc.z > 1.0f)
        {
            return false;
        }

        return TryNdcToImGuiPos(ndc, outImGuiPos, false);
    }

    bool TryProjectWorldToScreenWithCamera(const Assets::Camera& camera, const glm::vec3& worldPos, ImVec2& outImGuiPos)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (!viewport || viewport->Size.x <= 1.0f || viewport->Size.y <= 1.0f)
        {
            return false;
        }

        const float aspect = viewport->Size.x / viewport->Size.y;
        const float fov = camera.FieldOfView > 1.0f ? camera.FieldOfView : 60.0f;
        const glm::mat4 projection =
            glm::perspective(glm::radians(fov), aspect, std::max(0.05f, camera.NearPlane), camera.FarPlane);
        const glm::mat4 viewProjection = projection * camera.ModelView;
        const glm::vec4 clip = viewProjection * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0f)
        {
            return false;
        }

        return TryNdcToImGuiPos(glm::vec3(clip) / clip.w, outImGuiPos, true);
    }

    bool TryProjectWorldToScreenForGame(const NextGameInstanceBase& gameInstance, const glm::vec3& worldPos, ImVec2& outImGuiPos)
    {
        Assets::Camera camera{};
        if (!gameInstance.OverrideRenderCamera(camera))
        {
            return TryProjectWorldToScreen(worldPos, outImGuiPos);
        }

        return TryProjectWorldToScreenWithCamera(camera, worldPos, outImGuiPos);
    }

    void GetScreenToWorldRay(glm::vec2 locationSS, glm::vec3& org, glm::vec3& dir)
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            org = {};
            dir = {};
            return;
        }
        auto vkoffset = engine->GetRenderer().SwapChain().OutputOffset();
        auto vkextent = engine->GetRenderer().SwapChain().OutputExtent();
        glm::vec2 offset = {vkoffset.x, vkoffset.y};
        glm::vec2 extent = {vkextent.width, vkextent.height};
        glm::vec2 pixel = locationSS - glm::vec2(offset.x, offset.y);
        glm::vec2 uv = pixel / extent * glm::vec2(2.0, 2.0) - glm::vec2(1.0, 1.0);
        const auto& prevUBO = engine->GetLastUniformBufferObject();
        glm::vec4 origin = prevUBO.ModelViewInverse * glm::vec4(0, 0, 0, 1);
        glm::vec4 target = prevUBO.ProjectionInverse * (glm::vec4(uv.x, uv.y, 1, 1));
        glm::vec3 raydir = prevUBO.ModelViewInverse * glm::vec4(normalize((glm::vec3(target) - glm::vec3(0.0f, 0.0f, 0.0f))), 0.0f);
        org = glm::vec3(origin);
        dir = raydir;
    }

    void GetScreenToWorldRayWithCamera(const Assets::Camera& camera, glm::vec2 locationSS, glm::vec2 viewportPos,
                                       glm::vec2 viewportSize, glm::vec3& org, glm::vec3& dir)
    {
        if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
        {
            org = {};
            dir = {};
            return;
        }

        const glm::vec2 pixel = locationSS - viewportPos;
        const glm::vec2 uv = pixel / viewportSize * glm::vec2(2.0f, 2.0f) - glm::vec2(1.0f, 1.0f);
        glm::mat4 projection = glm::perspective(
            glm::radians(camera.FieldOfView),
            viewportSize.x / viewportSize.y,
            std::max(0.05f, camera.NearPlane),
            camera.FarPlane);
        projection[1][1] *= -1.0f;

        const glm::mat4 modelViewInverse = glm::inverse(camera.ModelView);
        const glm::mat4 projectionInverse = glm::inverse(projection);
        const glm::vec4 origin = modelViewInverse * glm::vec4(0, 0, 0, 1);
        const glm::vec4 target = projectionInverse * glm::vec4(uv.x, uv.y, 1, 1);
        const glm::vec3 raydir = modelViewInverse *
            glm::vec4(normalize(glm::vec3(target) - glm::vec3(0.0f, 0.0f, 0.0f)), 0.0f);

        org = glm::vec3(origin);
        dir = raydir;
    }

    void DrawAuxLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size, bool depthTest)
    {
        if (NextEngine* engine = NextEngine::GetInstance(); engine && engine->GetDebugDraw())
        {
            engine->GetDebugDraw()->AddLine(from, to, color, size, depthTest);
        }
    }

    void DrawAuxBox(glm::vec3 min, glm::vec3 max, glm::vec4 color, float size, bool depthTest)
    {
        if (NextEngine* engine = NextEngine::GetInstance(); engine && engine->GetDebugDraw())
        {
            engine->GetDebugDraw()->AddBox(min, max, color, size, depthTest);
        }
    }

    void DrawAuxPoint(glm::vec3 location, glm::vec4 color, float size, int32_t durationInTick, bool depthTest)
    {
        if (NextEngine* engine = NextEngine::GetInstance(); engine && engine->GetDebugDraw())
        {
            engine->GetDebugDraw()->AddPoint(location, color, size, durationInTick, depthTest);
        }
    }
}
