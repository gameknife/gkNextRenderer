#include "Runtime/Utilities/PhysicsDebugOverlay.hpp"

#include <cmath>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Engine.hpp"
#include "Runtime/Subsystems/NextCharacterController.h"
#include "Runtime/Subsystems/NextPhysics.h"

namespace
{
    ImU32 ToImU32(const glm::vec4& color)
    {
        const auto clampByte = [](float value) -> int
        {
            return static_cast<int>(std::round(glm::clamp(value, 0.0f, 1.0f) * 255.0f));
        };

        return IM_COL32(clampByte(color.r), clampByte(color.g), clampByte(color.b), clampByte(color.a));
    }

    struct FOverlayProjector
    {
        ImVec2 viewportSize;
        ImVec2 viewportPos;
        glm::mat4 viewProjection;

        bool Project(const glm::vec3& worldPos, ImVec2& screenPos) const
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
        }
    };

    std::optional<FOverlayProjector> BuildOverlayProjector(const Assets::Camera& camera)
    {
        ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
        ImVec2 viewportPos = ImGui::GetMainViewport()->Pos;
        if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
        {
            return std::nullopt;
        }

        const float aspect = viewportSize.x / viewportSize.y;
        const float fov = camera.FieldOfView > 1.0f ? camera.FieldOfView : 60.0f;
        const glm::mat4 projection = glm::perspective(glm::radians(fov), aspect, 0.05f, 2000.0f);
        return FOverlayProjector{
            viewportSize,
            viewportPos,
            projection * camera.ModelView,
        };
    }

    void DrawProjectedLine(ImDrawList* drawList, const FOverlayProjector& projector, const glm::vec3& a,
                           const glm::vec3& b, ImU32 color, float thickness)
    {
        ImVec2 sa;
        ImVec2 sb;
        if (projector.Project(a, sa) && projector.Project(b, sb))
        {
            drawList->AddLine(sa, sb, color, thickness);
        }
    }

    float ComputeCapsuleRadiusAtHeight(float localY, float height, float radius)
    {
        if (localY < radius)
        {
            const float delta = localY - radius;
            return std::sqrt(std::max(0.0f, radius * radius - delta * delta));
        }

        const float upperHemisphereStart = height - radius;
        if (localY > upperHemisphereStart)
        {
            const float delta = localY - upperHemisphereStart;
            return std::sqrt(std::max(0.0f, radius * radius - delta * delta));
        }

        return radius;
    }

    struct FPhysicsLegendEntry
    {
        const char* label;
        glm::vec4 color;
    };

    enum class EPhysicsLegendCategory : uint8_t
    {
        Static = 0,
        Kinematic,
        DynamicAwake,
        DynamicSleeping,
        Hidden,
        Count
    };

    struct FPhysicsDebugStats
    {
        std::array<int, static_cast<size_t>(EPhysicsLegendCategory::Count)> counts{};
        int total = 0;

        void Add(EPhysicsLegendCategory category)
        {
            counts[static_cast<size_t>(category)]++;
            total++;
        }
    };

    EPhysicsLegendCategory ClassifyBodyDebugState(const FNextPhysicsDebugState& state)
    {
        if (!state.isValid || state.objectLayer == NextLayers::HIDDEN)
        {
            return EPhysicsLegendCategory::Hidden;
        }

        switch (state.motionType)
        {
        case NextMotionType::Static:
            return EPhysicsLegendCategory::Static;
        case NextMotionType::Kinematic:
            return EPhysicsLegendCategory::Kinematic;
        case NextMotionType::Dynamic:
            return state.isActive ? EPhysicsLegendCategory::DynamicAwake : EPhysicsLegendCategory::DynamicSleeping;
        default:
            return EPhysicsLegendCategory::Hidden;
        }
    }

    std::array<FPhysicsLegendEntry, 5> BuildPhysicsLegendEntries()
    {
        return {
            FPhysicsLegendEntry{"Static", glm::vec4(0.35f, 0.65f, 1.0f, 1.0f)},
            FPhysicsLegendEntry{"Kinematic", glm::vec4(0.2f, 0.95f, 0.95f, 1.0f)},
            FPhysicsLegendEntry{"Dynamic (Awake)", glm::vec4(0.25f, 1.0f, 0.35f, 1.0f)},
            FPhysicsLegendEntry{"Dynamic (Sleeping)", glm::vec4(1.0f, 0.75f, 0.2f, 1.0f)},
            FPhysicsLegendEntry{"Hidden / Disabled", glm::vec4(1.0f, 0.2f, 0.2f, 1.0f)},
        };
    }

    void DrawPhysicsDebugLegend(const FPhysicsDebugStats& stats)
    {
        const auto legendEntries = BuildPhysicsLegendEntries();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const float margin = 12.0f;

        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x - margin, viewport->Pos.y + margin),
            ImGuiCond_Always,
            ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.72f);

        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoInputs;

        if (ImGui::Begin("Physics Debug Legend", nullptr, flags))
        {
            ImGui::TextUnformatted("Physics Debug");
            ImGui::Separator();
            for (size_t i = 0; i < legendEntries.size(); ++i)
            {
                const auto& entry = legendEntries[i];
                ImGui::ColorButton(entry.label, ImVec4(entry.color.r, entry.color.g, entry.color.b, entry.color.a),
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(12.0f, 12.0f));
                ImGui::SameLine();
                ImGui::Text("%s: %d", entry.label, stats.counts[i]);
            }
            ImGui::Separator();
            ImGui::Text("Total Bodies: %d", stats.total);
            ImGui::TextUnformatted("Circle: body center / mass center");
        }
        ImGui::End();
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

    const std::optional<FOverlayProjector> projector = BuildOverlayProjector(camera);
    if (!projector.has_value())
    {
        return;
    }

    auto* drawList = ImGui::GetForegroundDrawList();
    FPhysicsDebugStats stats;

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
        if (!renderComp || !physComp || !renderComp->IsDrawable())
        {
            continue;
        }

        auto* body = physics->GetBody(physComp->GetPhysicsBody());
        if (!body)
        {
            continue;
        }

        const FNextPhysicsDebugState debugState = physics->GetBodyDebugState(physComp->GetPhysicsBody());
        stats.Add(ClassifyBodyDebugState(debugState));

        if (!renderComp->GetVisible())
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

        const ImU32 color = ToImU32(physics->GetBodyDebugColor(physComp->GetPhysicsBody()));
        for (const auto& edge : kEdges)
        {
            DrawProjectedLine(drawList, *projector, corners[edge[0]], corners[edge[1]], color, 1.5f);
        }

        ImVec2 center;
        if (projector->Project(body->position, center))
        {
            drawList->AddCircle(center, 3.5f, color, 10, 1.5f);
        }
    }

    DrawPhysicsDebugLegend(stats);
#else
    (void)scene;
    (void)camera;
#endif
}

void Runtime::DrawCharacterControllerDebugOverlay(const NextCharacterController& controller, const Assets::Camera& camera)
{
#if WITH_PHYSIC
    if (!controller.IsValid())
    {
        return;
    }

    const std::optional<FOverlayProjector> projector = BuildOverlayProjector(camera);
    if (!projector.has_value())
    {
        return;
    }

    const float height = controller.GetHeight();
    const float radius = controller.GetRadius();
    if (height <= 0.0f || radius <= 0.0f)
    {
        return;
    }

    auto* drawList = ImGui::GetForegroundDrawList();
    const glm::vec3 basePosition = controller.GetPosition();
    const ImU32 color = controller.IsOnGround() ? IM_COL32(90, 255, 140, 230) : IM_COL32(255, 180, 70, 230);

    constexpr int kVerticalSamples = 18;
    constexpr int kRingSegments = 24;
    constexpr float kLineThickness = 1.5f;
    const std::array<float, 4> meridians = {
        0.0f,
        glm::half_pi<float>(),
        glm::pi<float>(),
        glm::half_pi<float>() * 3.0f,
    };

    for (float phi : meridians)
    {
        std::vector<glm::vec3> points;
        points.reserve(kVerticalSamples + 1);
        for (int i = 0; i <= kVerticalSamples; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kVerticalSamples);
            const float localY = t * height;
            const float ringRadius = ComputeCapsuleRadiusAtHeight(localY, height, radius);
            points.emplace_back(basePosition.x + std::cos(phi) * ringRadius,
                                basePosition.y + localY,
                                basePosition.z + std::sin(phi) * ringRadius);
        }

        for (size_t i = 1; i < points.size(); ++i)
        {
            DrawProjectedLine(drawList, *projector, points[i - 1], points[i], color, kLineThickness);
        }
    }

    const std::array<float, 5> ringHeights = {
        radius * 0.35f,
        radius,
        height * 0.5f,
        height - radius,
        height - radius * 0.35f,
    };

    for (float localY : ringHeights)
    {
        const float ringRadius = ComputeCapsuleRadiusAtHeight(localY, height, radius);
        if (ringRadius <= 0.001f)
        {
            continue;
        }

        std::vector<glm::vec3> ringPoints;
        ringPoints.reserve(kRingSegments + 1);
        for (int segment = 0; segment <= kRingSegments; ++segment)
        {
            const float angle = glm::two_pi<float>() * static_cast<float>(segment) / static_cast<float>(kRingSegments);
            ringPoints.emplace_back(basePosition.x + std::cos(angle) * ringRadius,
                                    basePosition.y + localY,
                                    basePosition.z + std::sin(angle) * ringRadius);
        }

        for (size_t i = 1; i < ringPoints.size(); ++i)
        {
            DrawProjectedLine(drawList, *projector, ringPoints[i - 1], ringPoints[i], color, kLineThickness);
        }
    }

    ImVec2 center;
    if (projector->Project(basePosition + glm::vec3(0.0f, height * 0.5f, 0.0f), center))
    {
        drawList->AddCircle(center, 3.5f, color, 12, kLineThickness);
    }
#else
    (void)controller;
    (void)camera;
#endif
}
