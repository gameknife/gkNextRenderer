#include "Modules/DevTools/PhysicsDebugOverlay.hpp"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <fmt/format.h>

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

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
        auto* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return;
        }

        const auto legendEntries = BuildPhysicsLegendEntries();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        constexpr float distance = 12.0f;
        constexpr float panelWidth = 360.0f;

        const bool f3Open = engine->GetUserSettings().ShowOverlay;
        const bool f2Open = engine->GetShowFlags().DebugGraphicsPanel;
        float rightOffset = distance;
        if (f3Open)
        {
            rightOffset += 380.0f + 10.0f;
        }
        if (f2Open)
        {
            rightOffset += 380.0f + 10.0f;
        }

        const ImVec2 pos = ImVec2(viewport->Pos.x + viewport->Size.x - rightOffset - panelWidth,
                                  viewport->Pos.y + distance + 44.0f);
        const float panelHeight = std::max(420.0f, viewport->Size.y - distance - 86.0f);

        NextUI::Theme::FDetailPanelConfig panelConfig{};
        panelConfig.WindowId = "##PhysicsDebugPanel";
        panelConfig.ContentWindowId = "##PhysicsDebugContent";
        panelConfig.Icon = ICON_FA_ATOM;
        panelConfig.Title = "Physics Debug";
        panelConfig.Open = &engine->GetShowFlags().DebugPhysicsOverlay;
        panelConfig.Position = pos;
        panelConfig.Size = ImVec2(panelWidth, panelHeight);

        if (!NextUI::Theme::BeginDetailPanel(panelConfig))
        {
            return;
        }

        constexpr float cardHorizontalInset = 4.0f;
        auto BeginCard = [&](const char* id, float height = 0.0f, ImGuiWindowFlags extraFlags = 0)
        {
            ImGui::Dummy(ImVec2(0.0f, 2.0f));
            const float cardWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - cardHorizontalInset * 2.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cardHorizontalInset);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceElevated, 0.38f));
            ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.84f));
            if (height > 0.0f)
            {
                ImGui::BeginChild(id, ImVec2(cardWidth, height), ImGuiChildFlags_Borders, extraFlags);
            }
            else
            {
                ImGui::BeginChild(id, ImVec2(cardWidth, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, extraFlags);
            }
        };

        auto EndCard = [&]()
        {
            ImGui::EndChild();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
        };

        const ImVec4 colHeader = NextUI::Theme::Color(NextUI::Theme::EColor::Blue);
        const ImVec4 colLabel = NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted);
        const ImVec4 colVal = NextUI::Theme::Color(NextUI::Theme::EColor::Text);

        auto CompactStat = [&](const char* label, const std::string& value)
        {
            ImGui::TextColored(colLabel, "%s", label);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(colVal, "%s", value.c_str());
        };

        // 1. Bodies & Layers Card
        BeginCard("##PhysicsBodiesCard", 0.0f);
        ImGui::TextColored(colHeader, "%s", LOCTEXT("Bodies & Layers"));
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        if (ImGui::BeginTable("##PhysicsOverviewTable", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            CompactStat(LOCTEXT("Total Bodies"), std::to_string(stats.total));

            ImGui::TableSetColumnIndex(1);
            const int awakeCount = stats.counts[static_cast<size_t>(EPhysicsLegendCategory::DynamicAwake)];
            const int sleepingCount = stats.counts[static_cast<size_t>(EPhysicsLegendCategory::DynamicSleeping)];
            CompactStat(LOCTEXT("Awake / Sleep"), fmt::format("{} / {}", awakeCount, sleepingCount));
            ImGui::EndTable();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (ImGui::BeginTable("##PhysicsCategoriesTable", 3, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Badge", ImGuiTableColumnFlags_WidthFixed, 20.0f);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 45.0f);

            for (size_t i = 0; i < legendEntries.size(); ++i)
            {
                const auto& entry = legendEntries[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::ColorButton(entry.label, ImVec4(entry.color.r, entry.color.g, entry.color.b, entry.color.a),
                                   ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                                   ImVec2(14.0f, 14.0f));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(colVal, "%s", entry.label);
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(colLabel, "%d", stats.counts[i]);
            }
            ImGui::EndTable();
        }
        EndCard();

        // 2. Simulation State & Controls Card
        BeginCard("##PhysicsSimulationCard", 0.0f);
        ImGui::TextColored(colHeader, "%s", LOCTEXT("Simulation"));
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        auto* physics = engine->GetPhysicsEngine();
        FNextPhysicsBodyStats physicsStats{};
        if (physics != nullptr)
        {
            physicsStats = physics->GetBodyStats();
        }

        if (ImGui::BeginTable("##PhysicsSimStateTable", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const std::string backendStr = physics ? "Jolt Physics" : LOCTEXT("Inactive");
            CompactStat(LOCTEXT("Backend"), backendStr);

            ImGui::TableSetColumnIndex(1);
            const bool isPaused = physics ? physics->IsPaused() : true;
            const std::string stateStr = isPaused ? LOCTEXT("Paused") : LOCTEXT("Running");
            CompactStat(LOCTEXT("State"), stateStr);

            if (physicsStats.simulatedSteps > 0)
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                CompactStat(LOCTEXT("Sim Steps"), std::to_string(physicsStats.simulatedSteps));

                ImGui::TableSetColumnIndex(1);
                CompactStat(LOCTEXT("Updates"), std::to_string(physicsStats.updateCalls));
            }
            ImGui::EndTable();
        }

        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        bool tickPhysics = engine->GetUserSettings().TickPhysics;
        if (ImGui::Checkbox(LOCTEXT("Tick Physics Simulation"), &tickPhysics))
        {
            engine->GetUserSettings().TickPhysics = tickPhysics;
        }
        EndCard();

        // 3. 3D Gizmo Legend Card
        BeginCard("##PhysicsGizmoCard", 0.0f);
        ImGui::TextColored(colHeader, "%s", LOCTEXT("3D Gizmo Legend"));
        ImGui::Dummy(ImVec2(0.0f, 2.0f));

        if (ImGui::BeginTable("##GizmoLegendTable", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(colLabel, "%s", LOCTEXT("Wireframe Box"));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(colVal, "%s", LOCTEXT("Oriented Bounding Box"));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(colLabel, "%s", LOCTEXT("Center Dot"));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(colVal, "%s", LOCTEXT("Center of Mass"));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(colLabel, "%s", LOCTEXT("Color Coding"));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(colVal, "%s", LOCTEXT("Layer & Motion State"));

            ImGui::EndTable();
        }
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::TextDisabled("[F1] %s", LOCTEXT("Toggle Physics Debug"));
        EndCard();

        NextUI::Theme::EndDetailPanel();
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

    // Include backend-owned bodies (vehicles, controllers, procedural bodies) that do not
    // necessarily have a one-to-one scene node / PhysicsComponent representation.
    physics->DrawDebugBodies();

    static constexpr int kEdges[12][2] = {
        {0, 1}, {1, 3}, {3, 2}, {2, 0}, 
        {4, 5}, {5, 7}, {7, 6}, {6, 4},
        {0, 4}, {1, 5}, {2, 6}, {3, 7},
    };
    static constexpr bool physicsDebugDepthTest = true;

    for (const auto* physComp : scene.Components<Runtime::PhysicsComponent>())
    {
        const Assets::Node* node = physComp->GetOwner();
        if (!node)
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

        auto renderComp = node->GetComponent<Runtime::RenderComponent>();
        if (!renderComp || !renderComp->IsDrawable())
        {
            continue;
        }

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
