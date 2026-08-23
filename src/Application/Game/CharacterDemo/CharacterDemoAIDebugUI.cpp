#include "CharacterDemoAIDebugUI.hpp"

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"

namespace CharacterDemoAIDebugUI
{
    namespace
    {
        using EBehaviorDebugState = NextGameplay::EBehaviorDebugState;
    }

    void DrawMenu(const FContext& context)
    {
        if (!context.showAIDebugMenu)
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 280.0f, viewport->WorkPos.y + 14.0f),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(262.0f, 0.0f), ImGuiCond_Always);

        if (ImGui::Begin("AI Debug Menu", nullptr,
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::TextUnformatted("F8 toggles this menu");
            ImGui::Separator();
            ImGui::Text("1 - Behavior Tree Overlay [%s]", context.showBehaviorTreeDebug ? "On" : "Off");
            ImGui::Text("2 - NavGrid Overlay [%s]", context.showNavGridDebug ? "On" : "Off");
            ImGui::TextDisabled("3-9 reserved for future AI debug toggles");
            ImGui::Text("0 - Close AI Debug Menu");
        }
        ImGui::End();
    }

    void DrawBehaviorTreeOverlay(const FContext& context)
    {
        if (!context.showBehaviorTreeDebug || !context.agent)
        {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 overlaySize(760.0f, 360.0f);
        const ImVec2 overlayPos(
            viewport->WorkPos.x + viewport->WorkSize.x - overlaySize.x - 18.0f,
            viewport->WorkPos.y + 112.0f);

        ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(overlaySize, ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::Begin(
            "##AIBehaviorTreeOverlay",
            nullptr,
            ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoBackground);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 base = ImGui::GetWindowPos();
        const float rounding = 12.0f;

        auto getStateColors = [](EBehaviorDebugState state, bool emphasize) -> std::pair<ImU32, ImU32>
        {
            ImVec4 border(0.45f, 0.48f, 0.54f, emphasize ? 0.95f : 0.55f);
            ImVec4 fill(0.07f, 0.09f, 0.12f, emphasize ? 0.88f : 0.68f);
            switch (state)
            {
            case EBehaviorDebugState::Failure:
                border = ImVec4(0.88f, 0.36f, 0.32f, emphasize ? 1.0f : 0.78f);
                fill = ImVec4(0.26f, 0.08f, 0.08f, emphasize ? 0.92f : 0.72f);
                break;
            case EBehaviorDebugState::Success:
                border = ImVec4(0.30f, 0.82f, 0.45f, emphasize ? 1.0f : 0.80f);
                fill = ImVec4(0.08f, 0.20f, 0.12f, emphasize ? 0.92f : 0.72f);
                break;
            case EBehaviorDebugState::Running:
                border = ImVec4(1.00f, 0.78f, 0.22f, emphasize ? 1.0f : 0.86f);
                fill = ImVec4(0.26f, 0.20f, 0.06f, emphasize ? 0.94f : 0.76f);
                break;
            case EBehaviorDebugState::Inactive:
            default:
                break;
            }
            return {ImGui::GetColorU32(fill), ImGui::GetColorU32(border)};
        };

        auto drawNodeBox = [&](const ImVec2& center,
                               const ImVec2& size,
                               const char* typeLabel,
                               const char* title,
                               const char* subtitle,
                               EBehaviorDebugState state,
                               bool emphasize)
        {
            const auto [fillColor, borderColor] = getStateColors(state, emphasize);
            const ImVec2 min(center.x - size.x * 0.5f, center.y - size.y * 0.5f);
            const ImVec2 max(center.x + size.x * 0.5f, center.y + size.y * 0.5f);
            const ImVec2 shadowOffset(0.0f, 10.0f);

            drawList->AddRectFilled(min + shadowOffset, max + shadowOffset, IM_COL32(0, 0, 0, 72), rounding + 2.0f);
            drawList->AddRectFilled(min, max, fillColor, rounding);
            drawList->AddRect(min, max, borderColor, rounding, 0, emphasize ? 3.0f : 2.0f);
            drawList->AddText(ImVec2(min.x + 14.0f, min.y + 10.0f), IM_COL32(235, 238, 242, emphasize ? 245 : 190), typeLabel);

            const ImVec2 titleSize = ImGui::CalcTextSize(title);
            drawList->AddText(ImVec2(center.x - titleSize.x * 0.5f, min.y + 32.0f), IM_COL32(248, 249, 250, 255), title);

            const ImVec2 subtitleSize = ImGui::CalcTextSize(subtitle);
            drawList->AddText(ImVec2(center.x - subtitleSize.x * 0.5f, min.y + 56.0f), IM_COL32(186, 194, 204, 220), subtitle);

            const char* stateLabel = NextGameplay::GetBehaviorDebugStateName(state);
            const ImVec2 stateSize = ImGui::CalcTextSize(stateLabel);
            const ImVec2 badgeMin(max.x - stateSize.x - 24.0f, min.y + 10.0f);
            const ImVec2 badgeMax(max.x - 12.0f, min.y + 28.0f);
            drawList->AddRectFilled(badgeMin, badgeMax, IM_COL32(255, 255, 255, emphasize ? 26 : 16), 9.0f);
            drawList->AddText(ImVec2(badgeMin.x + 8.0f, badgeMin.y + 2.0f), borderColor, stateLabel);

            return std::pair<ImVec2, ImVec2>(ImVec2(center.x, max.y), ImVec2(center.x, min.y));
        };

        auto drawConnection = [&](const ImVec2& from, const ImVec2& to, EBehaviorDebugState state, bool emphasize)
        {
            const auto [fillColor, borderColor] = getStateColors(state, emphasize);
            (void)fillColor;
            drawList->AddBezierCubic(from, ImVec2(from.x, from.y + 34.0f), ImVec2(to.x, to.y - 34.0f), to, borderColor, emphasize ? 3.0f : 2.0f);
        };

        drawList->AddText(ImVec2(base.x + 18.0f, base.y + 8.0f), IM_COL32(245, 246, 247, 255), "AI Behavior Tree");
        drawList->AddText(ImVec2(base.x + 18.0f, base.y + 28.0f), IM_COL32(180, 188, 198, 225),
                          "Runtime Overlay  |  Unreal-style debug view");

        std::string summary = "State: ";
        summary += context.aiEnabled && context.getStateName ? context.getStateName() : "Disabled";
        if (context.playerCharacter && context.aiCharacter && context.aiCharacter->controller.IsValid())
        {
            const glm::vec3 playerPos = context.playerCharacter->controller.GetPosition();
            const glm::vec3 aiPos = context.aiCharacter->controller.GetPosition();
            const float distance = glm::length(glm::vec2(playerPos.x - aiPos.x, playerPos.z - aiPos.z));
            summary += fmt::format("  |  Dist {:.1f}  |  Visible {}  |  LOS {}",
                                   distance,
                                   context.agent->GetTargetVisible() ? "Yes" : "No",
                                   (context.hasLineOfSightToPlayer && context.hasLineOfSightToPlayer()) ? "Yes" : "No");
        }
        drawList->AddText(ImVec2(base.x + 18.0f, base.y + 52.0f), IM_COL32(215, 221, 228, 230), summary.c_str());

        if (context.navGrid && context.navGrid->IsBuilt())
        {
            const std::string patrolDebug = fmt::format(
                "Patrol Reachable: {} | Req {} -> Sel {} | Tested {} | Waypoints {} | Dist {:.1f} | NearFallback {} | Abandoned {} | Stuck {:.2f}s | {:.2f} ms",
                context.agent->patrolReachableFound ? "Yes" : "No",
                context.agent->patrolRequestedIndex,
                context.agent->patrolSelectedIndex,
                context.agent->patrolCandidatesTested,
                context.agent->patrolWaypointCount,
                context.agent->patrolSelectedDistance,
                context.agent->patrolUsedNearFallback ? "Yes" : "No",
                context.agent->patrolAbandonedTarget ? "Yes" : "No",
                context.agent->patrolStuckTime,
                context.agent->patrolSelectionMs);
            drawList->AddText(ImVec2(base.x + 18.0f, base.y + 72.0f), IM_COL32(170, 210, 255, 230), patrolDebug.c_str());
        }

        const ImVec2 rootCenter(base.x + overlaySize.x * 0.50f, base.y + 102.0f);
        const ImVec2 selectorCenter(base.x + overlaySize.x * 0.50f, base.y + 186.0f);
        const ImVec2 leafRowY(0.0f, base.y + 292.0f);
        const ImVec2 leafSize(150.0f, 90.0f);
        const ImVec2 topSize(180.0f, 76.0f);
        const ImVec2 selectorSize(220.0f, 86.0f);

        const auto rootSockets = drawNodeBox(rootCenter, topSize, "ROOT", "BT Root", "CharacterDemo AI",
                                             context.agent->GetBehaviorRootStatus(),
                                             context.agent->GetBehaviorRootStatus() == EBehaviorDebugState::Running);
        const auto selectorSockets = drawNodeBox(selectorCenter, selectorSize, "SELECTOR", "Combat Root",
                                                 "Evade -> Attack -> Chase -> Patrol",
                                                 context.agent->GetBehaviorRootStatus(),
                                                 context.agent->GetBehaviorRootStatus() == EBehaviorDebugState::Running);

        drawConnection(rootSockets.first, selectorSockets.second, context.agent->GetBehaviorRootStatus(),
                       context.agent->GetBehaviorRootStatus() == EBehaviorDebugState::Running);

        struct FLeafDebugNode
        {
            const char* title;
            const char* subtitle;
            EBehaviorDebugState state;
            float x;
        };

        const std::array<FLeafDebugNode, 4> leafNodes{{
            {"Evade", "Too close, break contact", context.agent->GetBehaviorEvadeStatus(), 130.0f},
            {"Attack", "Hold lane and fire", context.agent->GetBehaviorAttackStatus(), 305.0f},
            {"Chase", "Close distance to target", context.agent->GetBehaviorChaseStatus(), 480.0f},
            {"Patrol", "Fallback route sweep", context.agent->GetBehaviorPatrolStatus(), 655.0f},
        }};

        for (const auto& leaf : leafNodes)
        {
            const ImVec2 center(base.x + leaf.x, leafRowY.y);
            const bool emphasize = leaf.state == EBehaviorDebugState::Running || leaf.state == EBehaviorDebugState::Success;
            const auto sockets = drawNodeBox(center, leafSize, "TASK", leaf.title, leaf.subtitle, leaf.state, emphasize);
            drawConnection(selectorSockets.first, sockets.second, leaf.state, emphasize);
        }

        ImGui::End();
    }

    void DrawNavGridOverlay(const FContext& context)
    {
        if (!context.showNavGridDebug || !context.navGrid || !context.agent || !context.aiCharacter || !context.navGrid->IsBuilt())
        {
            return;
        }

        Assets::Camera cam{};
        if (context.overrideRenderCamera)
        {
            context.overrideRenderCamera(cam);
        }

        const ImVec2 viewportSize = ImGui::GetMainViewport()->Size;
        const ImVec2 viewportPos = ImGui::GetMainViewport()->Pos;
        if (viewportSize.x <= 1.0f || viewportSize.y <= 1.0f)
        {
            return;
        }

        const float aspect = viewportSize.x / viewportSize.y;
        const float fov = cam.FieldOfView > 1.0f ? cam.FieldOfView : 60.0f;
        const glm::mat4 projection =
            Utilities::Math::ReverseZPerspective(glm::radians(fov), aspect, 0.05f, 2000.0f);
        const glm::mat4 viewProjection = projection * cam.ModelView;

        auto project = [&](const glm::vec3& worldPos, ImVec2& screenPos) -> bool
        {
            const glm::vec4 clip = viewProjection * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.0f)
            {
                return false;
            }
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f)
            {
                return false;
            }
            screenPos.x = viewportPos.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x;
            screenPos.y = viewportPos.y + (-ndc.y * 0.5f + 0.5f) * viewportSize.y;
            return true;
        };

        auto* drawList = ImGui::GetForegroundDrawList();
        const float cellSize = context.navGrid->GetCellSize();
        size_t reachableCount = 0;
        size_t disconnectedCount = 0;

        if (context.aiCharacter->controller.IsValid())
        {
            const glm::vec3 aiPos = context.aiCharacter->controller.GetPosition();
            constexpr float drawRadius = 18.0f;
            const int cellRadius = static_cast<int>(drawRadius / cellSize);
            const auto reachableMask = context.navGrid->BuildReachabilityMask(aiPos, aiPos.y);
            const glm::vec3 gridMin = context.navGrid->GetWorldMin();
            const glm::ivec2 aiCell = {
                static_cast<int>(std::floor((aiPos.x - gridMin.x) / cellSize)),
                static_cast<int>(std::floor((aiPos.z - gridMin.z) / cellSize))
            };

            const ImU32 reachableFill = IM_COL32(60, 210, 110, 52);
            const ImU32 reachableOutline = IM_COL32(60, 220, 120, 150);
            const ImU32 disconnectedFill = IM_COL32(255, 180, 40, 44);
            const ImU32 disconnectedOutline = IM_COL32(255, 190, 40, 132);
            const ImU32 aiCellColor = IM_COL32(80, 190, 255, 230);
            constexpr float liftY = 0.05f;

            for (int dz = -cellRadius; dz <= cellRadius; ++dz)
            {
                for (int dx = -cellRadius; dx <= cellRadius; ++dx)
                {
                    if ((dx * dx + dz * dz) > (cellRadius * cellRadius))
                    {
                        continue;
                    }

                    const int gx = aiCell.x + dx;
                    const int gz = aiCell.y + dz;
                    if (gx < 0 || gx >= context.navGrid->GetWidth() || gz < 0 || gz >= context.navGrid->GetHeight())
                    {
                        continue;
                    }

                    const glm::vec3 cellWorld = context.navGrid->GetCellWorldPosition(gx, gz);
                    const float halfExtent = cellSize * 0.46f;
                    std::array<ImVec2, 4> projectedCorners{};
                    const std::array<glm::vec2, 4> cornerOffsets = {
                        glm::vec2(-halfExtent, -halfExtent),
                        glm::vec2(halfExtent, -halfExtent),
                        glm::vec2(halfExtent, halfExtent),
                        glm::vec2(-halfExtent, halfExtent)
                    };

                    bool visible = true;
                    for (size_t cornerIndex = 0; cornerIndex < cornerOffsets.size(); ++cornerIndex)
                    {
                        const glm::vec2 offset = cornerOffsets[cornerIndex];
                        const glm::vec3 cornerWorld(cellWorld.x + offset.x, cellWorld.y + liftY, cellWorld.z + offset.y);
                        if (!project(cornerWorld, projectedCorners[cornerIndex]))
                        {
                            visible = false;
                            break;
                        }
                    }

                    if (!visible || !context.navGrid->IsCellWalkable(gx, gz))
                    {
                        continue;
                    }

                    const size_t cellIndex = static_cast<size_t>(gz * context.navGrid->GetWidth() + gx);
                    const bool isConnected = cellIndex < reachableMask.size() && reachableMask[cellIndex] != 0;
                    if (isConnected)
                    {
                        ++reachableCount;
                    }
                    else
                    {
                        ++disconnectedCount;
                    }

                    drawList->AddConvexPolyFilled(projectedCorners.data(), static_cast<int>(projectedCorners.size()),
                                                 isConnected ? reachableFill : disconnectedFill);
                    drawList->AddPolyline(projectedCorners.data(), static_cast<int>(projectedCorners.size()),
                                          isConnected ? reachableOutline : disconnectedOutline, true, 1.0f);

                    if (gx == aiCell.x && gz == aiCell.y)
                    {
                        ImVec2 screenCenter;
                        if (project(glm::vec3(cellWorld.x, cellWorld.y + 0.12f, cellWorld.z), screenCenter))
                        {
                            drawList->AddCircle(screenCenter, 5.0f, aiCellColor, 10, 2.0f);
                        }
                    }
                }
            }
        }

        const auto& waypoints = context.agent->pathFollower.waypoints;
        if (waypoints.size() >= 2)
        {
            constexpr float liftY = 0.15f;
            for (size_t i = 0; i + 1 < waypoints.size(); ++i)
            {
                ImVec2 sa, sb;
                glm::vec3 a = waypoints[i];
                glm::vec3 b = waypoints[i + 1];
                a.y += liftY;
                b.y += liftY;
                if (project(a, sa) && project(b, sb))
                {
                    drawList->AddLine(sa, sb, IM_COL32(255, 200, 0, 200), 2.5f);
                }
            }

            for (size_t i = 0; i < waypoints.size(); ++i)
            {
                ImVec2 sp;
                glm::vec3 wp = waypoints[i];
                wp.y += liftY;
                if (project(wp, sp))
                {
                    const bool isCurrent = (i == context.agent->pathFollower.currentIndex);
                    drawList->AddCircleFilled(sp, isCurrent ? 5.0f : 3.0f,
                                              isCurrent ? IM_COL32(0, 255, 128, 255) : IM_COL32(255, 255, 0, 220), 8);
                }
            }
        }

        if (context.aiCharacter->controller.IsValid())
        {
            char buf[160];
            snprintf(buf, sizeof(buf), "NavGrid: %dx%d | Connected: %zu | OtherWalkable: %zu | Path: %zu | WP: %zu",
                     context.navGrid->GetWidth(), context.navGrid->GetHeight(),
                     reachableCount, disconnectedCount, waypoints.size(), context.agent->pathFollower.currentIndex);
            drawList->AddText(ImVec2(viewportPos.x + 10.0f, viewportPos.y + viewportSize.y - 60.0f),
                              IM_COL32(255, 255, 200, 220), buf);
        }
    }
}
