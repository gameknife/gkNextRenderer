#include "NextEngineHelper.h"

#include "Engine.hpp"
#include "UserInterface.hpp"
#include "Vulkan/SwapChain.hpp"

namespace
{
    std::vector<int32_t> AuxCounter;
}

namespace NextEngineHelper
{
    glm::vec3 ProjectScreenToWorld(glm::vec2 locationSS)
    {
        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return {};
        }
        glm::vec3 org;
        glm::vec3 dir;
        GetScreenToWorldRay(locationSS, org, dir);
        return dir;
    }

    glm::vec3 ProjectWorldToScreen(glm::vec3 locationWS)
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
        auto transformedFrom = ProjectWorldToScreen(from);
        auto transformedTo = ProjectWorldToScreen(to);

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
                auto transformed = ProjectWorldToScreen(location);
                if (transformed.z < 1)
                {
                    NextEngine::GetInstance()->GetUserInterface()->DrawPoint(transformed.x, transformed.y, size, color);
                }
                return (AuxCounter[id] -= 1) <= 0;
            });
        }
        else
        {
            auto transformed = ProjectWorldToScreen(location);
            if (transformed.z < 1)
            {
                engine->GetUserInterface()->DrawPoint(transformed.x, transformed.y, size, color);
            }
        }
    }
}
