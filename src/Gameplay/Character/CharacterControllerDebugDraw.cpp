#include "Gameplay/Character/CharacterControllerDebugDraw.hpp"

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Gameplay/Character/NextCharacterController.h"

#include <cmath>
#include <optional>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

namespace
{
    struct FOverlayProjector
    {
        ImVec2 viewportSize;
        ImVec2 viewportPosition;
        glm::mat4 viewProjection;

        bool Project(const glm::vec3& worldPosition, ImVec2& screenPosition) const
        {
            const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
            if (clip.w <= 0.0f)
            {
                return false;
            }

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            screenPosition.x = viewportPosition.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
            screenPosition.y = viewportPosition.y + (-ndc.y * 0.5f + 0.5f) * viewportSize.y;
            return true;
        }
    };

    std::optional<FOverlayProjector> BuildOverlayProjector(const Assets::Camera& camera)
    {
        const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
        const ImVec2 viewportPosition = ImGui::GetMainViewport()->Pos;
        if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
        {
            return std::nullopt;
        }

        const float aspect = viewportSize.x / viewportSize.y;
        const float fieldOfView = camera.FieldOfView > 1.0f ? camera.FieldOfView : 60.0f;
        const glm::mat4 projection =
            Utilities::Math::ReverseZPerspective(glm::radians(fieldOfView), aspect, 0.05f, 2000.0f);
        return FOverlayProjector{viewportSize, viewportPosition, projection * camera.ModelView};
    }

    void DrawProjectedLine(ImDrawList* drawList, const FOverlayProjector& projector, const glm::vec3& from,
                           const glm::vec3& to, ImU32 color, float thickness)
    {
        ImVec2 screenFrom;
        ImVec2 screenTo;
        if (projector.Project(from, screenFrom) && projector.Project(to, screenTo))
        {
            drawList->AddLine(screenFrom, screenTo, color, thickness);
        }
    }

    float ComputeCapsuleRadiusAtHeight(float localHeight, float height, float radius)
    {
        if (localHeight < radius)
        {
            const float delta = localHeight - radius;
            return std::sqrt(std::max(0.0f, radius * radius - delta * delta));
        }

        const float upperHemisphereStart = height - radius;
        if (localHeight > upperHemisphereStart)
        {
            const float delta = localHeight - upperHemisphereStart;
            return std::sqrt(std::max(0.0f, radius * radius - delta * delta));
        }

        return radius;
    }
}

void NextGameplay::DrawCharacterControllerDebugOverlay(const NextCharacterController& controller,
                                                        const Assets::Camera& camera)
{
    if (!controller.IsValid())
    {
        return;
    }

    const std::optional<FOverlayProjector> projector = BuildOverlayProjector(camera);
    const float height = controller.GetHeight();
    const float radius = controller.GetRadius();
    if (!projector || height <= 0.0f || radius <= 0.0f)
    {
        return;
    }

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    const glm::vec3 basePosition = controller.GetPosition();
    const ImU32 color = controller.IsOnGround() ? IM_COL32(90, 255, 140, 230) : IM_COL32(255, 180, 70, 230);
    constexpr int verticalSamples = 18;
    constexpr int ringSegments = 24;
    constexpr float lineThickness = 1.5f;
    const std::array<float, 4> meridians = {
        0.0f,
        glm::half_pi<float>(),
        glm::pi<float>(),
        glm::half_pi<float>() * 3.0f,
    };

    for (float phi : meridians)
    {
        std::vector<glm::vec3> points;
        points.reserve(verticalSamples + 1);
        for (int index = 0; index <= verticalSamples; ++index)
        {
            const float localHeight = static_cast<float>(index) / static_cast<float>(verticalSamples) * height;
            const float ringRadius = ComputeCapsuleRadiusAtHeight(localHeight, height, radius);
            points.emplace_back(basePosition.x + std::cos(phi) * ringRadius,
                                basePosition.y + localHeight,
                                basePosition.z + std::sin(phi) * ringRadius);
        }

        for (size_t index = 1; index < points.size(); ++index)
        {
            DrawProjectedLine(drawList, *projector, points[index - 1], points[index], color, lineThickness);
        }
    }

    const std::array<float, 5> ringHeights = {
        radius * 0.35f,
        radius,
        height * 0.5f,
        height - radius,
        height - radius * 0.35f,
    };
    for (float localHeight : ringHeights)
    {
        const float ringRadius = ComputeCapsuleRadiusAtHeight(localHeight, height, radius);
        if (ringRadius <= 0.001f)
        {
            continue;
        }

        std::vector<glm::vec3> points;
        points.reserve(ringSegments + 1);
        for (int segment = 0; segment <= ringSegments; ++segment)
        {
            const float angle = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(ringSegments);
            points.emplace_back(basePosition.x + std::cos(angle) * ringRadius,
                                basePosition.y + localHeight,
                                basePosition.z + std::sin(angle) * ringRadius);
        }

        for (size_t index = 1; index < points.size(); ++index)
        {
            DrawProjectedLine(drawList, *projector, points[index - 1], points[index], color, lineThickness);
        }
    }

    ImVec2 center;
    if (projector->Project(basePosition + glm::vec3(0.0f, height * 0.5f, 0.0f), center))
    {
        drawList->AddCircle(center, 3.5f, color, 12, lineThickness);
    }
}
