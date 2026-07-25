#include "Engine/Common/CoreMinimal.hpp"

#include "AnchorDebugOverlay.h"

#include <fmt/format.h>
#include <imgui.h>

#include <array>
#include <cstdint>
#include <string>

namespace NextGameplay::Sim
{
    namespace
    {
        bool ProjectWorld(const glm::mat4& viewProjection, const ImVec2& viewportPosition,
                          const ImVec2& viewportSize, const glm::vec3& worldPosition, ImVec2& outScreen)
        {
            const glm::vec4 clip = viewProjection * glm::vec4(worldPosition, 1.0f);
            if (clip.w <= 0.0f)
            {
                return false;
            }
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (ndc.x < -1.2f || ndc.x > 1.2f || ndc.y < -1.2f || ndc.y > 1.2f)
            {
                return false;
            }
            outScreen = {
                viewportPosition.x + (ndc.x * 0.5f + 0.5f) * viewportSize.x,
                viewportPosition.y + (-ndc.y * 0.5f + 0.5f) * viewportSize.y,
            };
            return true;
        }

        ImU32 CategoryColor(std::string_view category)
        {
            constexpr std::array<ImU32, 10> palette = {
                IM_COL32(242, 166, 73, 255),  IM_COL32(82, 156, 235, 255),
                IM_COL32(232, 211, 82, 255),  IM_COL32(194, 126, 218, 255),
                IM_COL32(85, 204, 137, 255),  IM_COL32(70, 205, 210, 255),
                IM_COL32(240, 111, 146, 255), IM_COL32(156, 185, 92, 255),
                IM_COL32(235, 128, 92, 255),  IM_COL32(150, 157, 235, 255),
            };
            uint32_t hash = 2166136261u;
            for (const char value : category)
            {
                hash = (hash ^ static_cast<uint8_t>(value)) * 16777619u;
            }
            return palette[hash % palette.size()];
        }

        template <typename Point, typename NameFn, typename CategoryFn, typename PositionFn, typename EnabledFn>
        void DrawPoints(const glm::mat4& viewProjection, std::span<const Point> points,
                        const FAnchorDebugDrawConfig& config, NameFn nameOf, CategoryFn categoryOf,
                        PositionFn positionOf, EnabledFn enabledOf)
        {
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (viewport == nullptr || viewport->Size.x <= 1.0f || viewport->Size.y <= 1.0f)
            {
                return;
            }

            ImDrawList* drawList = ImGui::GetBackgroundDrawList(viewport);
            for (const Point& point : points)
            {
                const glm::vec3 worldPosition = positionOf(point);
                ImVec2 groundScreen;
                ImVec2 labelScreen;
                if (!ProjectWorld(viewProjection, viewport->Pos, viewport->Size, worldPosition, groundScreen) ||
                    !ProjectWorld(viewProjection, viewport->Pos, viewport->Size,
                                  worldPosition + glm::vec3(0.0f, config.labelHeight, 0.0f), labelScreen))
                {
                    continue;
                }

                const ImU32 color = CategoryColor(categoryOf(point));
                drawList->AddLine({groundScreen.x - config.markerRadius, groundScreen.y},
                                  {groundScreen.x + config.markerRadius, groundScreen.y}, color, 2.0f);
                drawList->AddLine({groundScreen.x, groundScreen.y - config.markerRadius},
                                  {groundScreen.x, groundScreen.y + config.markerRadius}, color, 2.0f);
                drawList->AddCircle(groundScreen, config.markerRadius, color, 0, 2.0f);
                drawList->AddLine(groundScreen, labelScreen, color, 2.0f);
                drawList->AddCircleFilled(labelScreen, 4.0f, color);

                std::string label = fmt::format("{} [{}]", nameOf(point), categoryOf(point));
                if (config.showCoordinates)
                {
                    label += fmt::format(" ({:.2f}, {:.2f}, {:.2f})",
                                         worldPosition.x, worldPosition.y, worldPosition.z);
                }
                if (config.showDisabledState && !enabledOf(point))
                {
                    label += " DISABLED";
                }

                const ImVec2 textPosition(labelScreen.x + 7.0f,
                                          labelScreen.y - ImGui::GetTextLineHeight());
                const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                drawList->AddRectFilled({textPosition.x - 3.0f, textPosition.y - 2.0f},
                                        {textPosition.x + textSize.x + 3.0f,
                                         textPosition.y + textSize.y + 2.0f},
                                        IM_COL32(12, 16, 20, 210), 3.0f);
                drawList->AddText(textPosition, color, label.c_str());
            }
        }
    }

    void DrawAnchorDebugOverlay(const glm::mat4& viewProjection, std::span<const FAnchorPoi> points,
                                const FAnchorDebugDrawConfig& config)
    {
        DrawPoints(viewProjection, points, config,
                   [](const FAnchorPoi& point) -> std::string_view { return point.name; },
                   [](const FAnchorPoi& point) -> std::string_view { return point.category; },
                   [](const FAnchorPoi& point) { return point.worldPos; },
                   [](const FAnchorPoi& point) { return point.enabled; });
    }

    void DrawAnchorDebugOverlay(const glm::mat4& viewProjection, std::span<const FAnchorDebugPoint> points,
                                const FAnchorDebugDrawConfig& config)
    {
        DrawPoints(viewProjection, points, config,
                   [](const FAnchorDebugPoint& point) { return point.name; },
                   [](const FAnchorDebugPoint& point) { return point.category; },
                   [](const FAnchorDebugPoint& point) { return point.worldPos; },
                   [](const FAnchorDebugPoint& point) { return point.enabled; });
    }
}
