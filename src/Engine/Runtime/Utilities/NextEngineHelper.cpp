#include "Engine/Runtime/Utilities/NextEngineHelper.h"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <imgui.h>

namespace
{
    std::vector<int32_t> AuxCounter;

    glm::vec3 ProjectWorldToScreenInternal(glm::vec3 locationWS)
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return {};
        }

        auto vkoffset = engine->GetRenderer().SwapChain().OutputOffset();
        auto vkextent = engine->GetRenderer().SwapChain().OutputExtent();

        const auto& prevUBO = engine->GetUniformBufferObject();
        glm::vec4 transformed = prevUBO.ViewProjection * glm::vec4(locationWS, 1.0f);
        transformed = transformed / transformed.w;
        transformed.x += 1.0f;
        transformed.x *= vkextent.width / 2;
        transformed.y += 1.0f;
        transformed.y *= vkextent.height / 2;

        transformed.x += vkoffset.x;
        transformed.y += vkoffset.y;

        return transformed;
    }

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

namespace NextEngineHelper
{
    bool TryProjectWorldToScreen(const glm::vec3& worldPos, ImVec2& outImGuiPos)
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return false;
        }

        const auto& ubo = engine->GetUniformBufferObject();
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
        const auto& prevUBO = engine->GetUniformBufferObject();
        glm::vec4 origin = prevUBO.ModelViewInverse * glm::vec4(0, 0, 0, 1);
        glm::vec4 target = prevUBO.ProjectionInverse * (glm::vec4(uv.x, uv.y, 1, 1));
        glm::vec3 raydir = prevUBO.ModelViewInverse * glm::vec4(normalize((glm::vec3(target) - glm::vec3(0.0f, 0.0f, 0.0f))), 0.0f);
        org = glm::vec3(origin);
        dir = raydir;
    }

    void DrawAuxLine(glm::vec3 from, glm::vec3 to, glm::vec4 color, float size)
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return;
        }
        auto transformedFrom = ProjectWorldToScreenInternal(from);
        auto transformedTo = ProjectWorldToScreenInternal(to);

        if (transformedFrom.z < 1 && transformedTo.z < 1)
        {
            engine->GetUserInterface()->DrawLine(transformedFrom.x, transformedFrom.y, transformedTo.x, transformedTo.y, size, color);
        }
    }

    void DrawAuxBox(glm::vec3 min, glm::vec3 max, glm::vec4 color, float size)
    {
        DrawAuxLine(glm::vec3(min.x, min.y, min.z), glm::vec3(max.x, min.y, min.z), color, size);
        DrawAuxLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, max.y, min.z), color, size);
        DrawAuxLine(glm::vec3(max.x, max.y, min.z), glm::vec3(min.x, max.y, min.z), color, size);
        DrawAuxLine(glm::vec3(min.x, max.y, min.z), glm::vec3(min.x, min.y, min.z), color, size);

        DrawAuxLine(glm::vec3(min.x, min.y, max.z), glm::vec3(max.x, min.y, max.z), color, size);
        DrawAuxLine(glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, max.y, max.z), color, size);
        DrawAuxLine(glm::vec3(max.x, max.y, max.z), glm::vec3(min.x, max.y, max.z), color, size);
        DrawAuxLine(glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, min.y, max.z), color, size);

        DrawAuxLine(glm::vec3(min.x, min.y, min.z), glm::vec3(min.x, min.y, max.z), color, size);
        DrawAuxLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, min.y, max.z), color, size);
        DrawAuxLine(glm::vec3(max.x, max.y, min.z), glm::vec3(max.x, max.y, max.z), color, size);
        DrawAuxLine(glm::vec3(min.x, max.y, min.z), glm::vec3(min.x, max.y, max.z), color, size);
    }

    void DrawAuxPoint(glm::vec3 location, glm::vec4 color, float size, int32_t durationInTick)
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return;
        }
        if (durationInTick > 0)
        {
            AuxCounter.push_back(durationInTick);
            int32_t id = static_cast<int32_t>(AuxCounter.size()) - 1;
            engine->AddTickedTask([location, color, size, id](double deltaSeconds)->bool
            {
                auto transformed = ProjectWorldToScreenInternal(location);
                if (transformed.z < 1)
                {
                    NextEngine::GetInstance()->GetUserInterface()->DrawPoint(transformed.x, transformed.y, size, color);
                }
                return (AuxCounter[id] -= 1) <= 0;
            });
        }
        else
        {
            auto transformed = ProjectWorldToScreenInternal(location);
            if (transformed.z < 1)
            {
                engine->GetUserInterface()->DrawPoint(transformed.x, transformed.y, size, color);
            }
        }
    }
}
