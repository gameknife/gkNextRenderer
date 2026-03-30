#include "Runtime/Utilities/PhysicsDebugOverlay.hpp"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/NextPhysics.h"

namespace
{
    ImU32 GetDebugColor(Runtime::ENodeMobility mobility)
    {
        switch (mobility)
        {
        case Runtime::ENodeMobility::Dynamic:
            return IM_COL32(80, 255, 120, 220);
        case Runtime::ENodeMobility::Kinematic:
            return IM_COL32(255, 210, 70, 220);
        case Runtime::ENodeMobility::Static:
        default:
            return IM_COL32(90, 180, 255, 220);
        }
    }
}

void Runtime::DrawPhysicsDebugOverlay(const Assets::Scene& scene, const Assets::Camera& camera)
{
#if WITH_PHYSIC
    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    if (!physics)
    {
        return;
    }

    ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
    ImVec2 viewportPos = ImGui::GetMainViewport()->Pos;
    if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
    {
        return;
    }

    const float aspect = viewportSize.x / viewportSize.y;
    const float fov = camera.FieldOfView > 1.0f ? camera.FieldOfView : 60.0f;
    const glm::mat4 projection = glm::perspective(glm::radians(fov), aspect, 0.05f, 2000.0f);
    const glm::mat4 viewProjection = projection * camera.ModelView;

    auto* drawList = ImGui::GetForegroundDrawList();

    auto projectToScreen = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool
    {
        const glm::vec4 clip = viewProjection * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0.0f)
        {
            return false;
        }

        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        screenPos.x = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
        screenPos.y = viewportPos.y + (-ndc.y * 0.5f + 0.5f) * viewportSize.y;
        return true;
    };

    auto drawEdge = [&](const glm::vec3& a, const glm::vec3& b, ImU32 color)
    {
        ImVec2 sa;
        ImVec2 sb;
        if (projectToScreen(a, sa) && projectToScreen(b, sb))
        {
            drawList->AddLine(sa, sb, color, 1.5f);
        }
    };

    static constexpr int kEdges[12][2] = {
        {0, 1}, {1, 3}, {3, 2}, {2, 0},
        {4, 5}, {5, 7}, {7, 6}, {6, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };

    for (const auto& node : scene.Nodes())
    {
        if (!node)
        {
            continue;
        }

        auto renderComp = node->GetComponent<Runtime::RenderComponent>();
        auto physComp = node->GetComponent<Runtime::PhysicsComponent>();
        if (!renderComp || !physComp || !renderComp->GetVisible() || !renderComp->IsDrawable())
        {
            continue;
        }

        auto* body = physics->GetBody(physComp->GetPhysicsBody());
        if (!body)
        {
            continue;
        }

        const Assets::Model* model = scene.GetModel(renderComp->GetModelId());
        if (!model)
        {
            continue;
        }

        const glm::vec3 localMin = model->GetLocalAABBMin();
        const glm::vec3 localMax = model->GetLocalAABBMax();
        const glm::vec3 worldScale = node->WorldScale();
        const glm::vec3 scaledOffset = physComp->GetPhysicsOffset() * worldScale;
        const glm::vec3 worldTranslation = body->position - body->rotation * scaledOffset;
        const glm::mat4 worldTransform =
            glm::translate(glm::mat4(1.0f), worldTranslation) *
            glm::mat4_cast(body->rotation) *
            glm::scale(glm::mat4(1.0f), worldScale);

        glm::vec3 corners[8];
        corners[0] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMin.y, localMin.z, 1.0f));
        corners[1] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMin.y, localMin.z, 1.0f));
        corners[2] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMax.y, localMin.z, 1.0f));
        corners[3] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMax.y, localMin.z, 1.0f));
        corners[4] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMin.y, localMax.z, 1.0f));
        corners[5] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMin.y, localMax.z, 1.0f));
        corners[6] = glm::vec3(worldTransform * glm::vec4(localMin.x, localMax.y, localMax.z, 1.0f));
        corners[7] = glm::vec3(worldTransform * glm::vec4(localMax.x, localMax.y, localMax.z, 1.0f));

        const ImU32 color = GetDebugColor(physComp->GetMobility());
        for (const auto& edge : kEdges)
        {
            drawEdge(corners[edge[0]], corners[edge[1]], color);
        }

        ImVec2 center;
        if (projectToScreen(body->position, center))
        {
            drawList->AddCircle(center, 3.5f, color, 10, 1.5f);
        }
    }
#else
    (void)scene;
    (void)camera;
#endif
}
