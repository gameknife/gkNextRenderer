#include "Modules/DevTools/PhysicsDebugOverlay.hpp"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.h"
#include "Engine/Runtime/Utilities/NextEngineHelper.h"

namespace
{
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

void Runtime::DrawPhysicsDebugOverlay(const Assets::Scene& scene, const Assets::Camera&)
{
    auto* physics = NextEngine::GetInstance()->GetPhysicsEngine();
    if (!physics)
    {
        return;
    }

    FPhysicsDebugStats stats;

    static constexpr int kEdges[12][2] = {
        {0, 1}, {1, 3}, {3, 2}, {2, 0}, 
        {4, 5}, {5, 7}, {7, 6}, {6, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    static constexpr bool physicsDebugDepthTest = true;

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

        if (!renderComp->GetVisible() && debugState.objectLayer == NextLayers::HIDDEN)
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

        const glm::vec4 color = physics->GetBodyDebugColor(physComp->GetPhysicsBody());
        for (const auto& edge : kEdges)
        {
            EngineHelper::DrawAuxLine(corners[edge[0]], corners[edge[1]], color, 1, physicsDebugDepthTest);
        }

        EngineHelper::DrawAuxPoint(body->position, color, 2, 0, false);
    }

    DrawPhysicsDebugLegend(stats);
}
