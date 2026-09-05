#pragma once
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <string>
#include <fmt/format.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>
#include "Engine/Runtime/Config/ShowFlags.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"

namespace Utilities
{
    namespace UI
    {
        struct FModalPopupOptions
        {
            ImVec2 RequestedSize = ImVec2(0.0f, 0.0f);
            ImGuiCond SizeCond = ImGuiCond_Appearing;
            ImGuiCond PosCond = ImGuiCond_Appearing;
            bool CenterInAnchorViewport = true;
            bool ClampToAnchorViewport = true;
            float ViewportMargin = 16.0f;
        };

        inline bool BeginAnchoredPopupModal(const char* name,
                                            bool* pOpen = nullptr,
                                            ImGuiWindowFlags flags = 0,
                                            const FModalPopupOptions& options = {})
        {
            ImGuiViewport* anchorViewport = ImGui::GetWindowViewport();
            if (anchorViewport == nullptr)
            {
                anchorViewport = ImGui::GetMainViewport();
            }

            const ImVec2 workPos = anchorViewport->WorkPos;
            const ImVec2 workSize = anchorViewport->WorkSize;
            const ImVec2 maxSize = ImVec2(
                std::max(1.0f, workSize.x - options.ViewportMargin * 2.0f),
                std::max(1.0f, workSize.y - options.ViewportMargin * 2.0f));

            ImGui::SetNextWindowViewport(anchorViewport->ID);
            if (options.CenterInAnchorViewport)
            {
                const ImVec2 center = ImVec2(workPos.x + workSize.x * 0.5f, workPos.y + workSize.y * 0.5f);
                ImGui::SetNextWindowPos(center, options.PosCond, ImVec2(0.5f, 0.5f));
            }

            if (options.ClampToAnchorViewport)
            {
                ImGui::SetNextWindowSizeConstraints(ImVec2(1.0f, 1.0f), maxSize);
            }

            if (options.RequestedSize.x > 0.0f || options.RequestedSize.y > 0.0f)
            {
                const ImVec2 requestedSize = ImVec2(
                    options.RequestedSize.x > 0.0f ? std::min(options.RequestedSize.x, maxSize.x) : 0.0f,
                    options.RequestedSize.y > 0.0f ? std::min(options.RequestedSize.y, maxSize.y) : 0.0f);
                ImGui::SetNextWindowSize(requestedSize, options.SizeCond);
            }

            const bool open = ImGui::BeginPopupModal(name, pOpen, flags);
            if (!open || !options.ClampToAnchorViewport)
            {
                return open;
            }

            const ImVec2 minPos = ImVec2(workPos.x + options.ViewportMargin, workPos.y + options.ViewportMargin);
            const ImVec2 windowSize = ImGui::GetWindowSize();
            const ImVec2 maxPos = ImVec2(
                std::max(minPos.x, workPos.x + workSize.x - windowSize.x - options.ViewportMargin),
                std::max(minPos.y, workPos.y + workSize.y - windowSize.y - options.ViewportMargin));
            const ImVec2 currentPos = ImGui::GetWindowPos();
            const ImVec2 clampedPos = ImVec2(
                std::clamp(currentPos.x, minPos.x, maxPos.x),
                std::clamp(currentPos.y, minPos.y, maxPos.y));

            if (std::fabs(clampedPos.x - currentPos.x) > 0.5f || std::fabs(clampedPos.y - currentPos.y) > 0.5f)
            {
                ImGui::SetWindowPos(clampedPos);
            }

            return open;
        }

        inline void DrawShowFlagItem(const char* name, bool& flag) {
            if (ImGui::MenuItem(fmt::format("{} {}", flag ? ICON_FA_SQUARE_CHECK : ICON_FA_SQUARE, name).c_str(), NULL, false)) {
                flag = !flag;
            }
        }

        // Wireframe draws the soft-mesh expanded primitives, so these two decide how that geometry
        // reads: whether scene depth hides edges behind surfaces, and whether edges are tinted by
        // the LOD level each proxy was drawn at. Sub-items of the Wireframe toggle rather than
        // separate entries, since neither does anything while the overlay is off.
        inline void DrawWireframeOptions(Runtime::Config::UserSettings& settings, bool enabled) {
            if (!enabled)
            {
                return;
            }
            ImGui::Indent();
            DrawShowFlagItem("  X-Ray (ignore depth)", settings.WireframeXRay);
            bool tintByLod = settings.WireframeColorMode == 1;
            DrawShowFlagItem("  Tint by LOD", tintByLod);
            settings.WireframeColorMode = tintByLod ? 1u : 0u;
            ImGui::Unindent();
        }

        inline void DrawShowFlagsCommon(Runtime::Config::ShowFlags& showFlags,
                                        Runtime::Config::UserSettings* settings = nullptr) {
            DrawShowFlagItem("Grid", showFlags.ShowGrid);
            ImGui::Separator();
            DrawShowFlagItem("Lighting", showFlags.DebugDraw_Lighting);
            DrawShowFlagItem("Area Lights", showFlags.DebugDraw_AreaLights);
            DrawShowFlagItem("Bounding Box", showFlags.DebugDraw_BoundingBox);
            DrawShowFlagItem("Physics Bodies", showFlags.DebugDraw_PhysicsBodies);
            DrawShowFlagItem("Visual Debug", showFlags.ShowVisualDebug);
            DrawShowFlagItem("Edge", showFlags.ShowEdge);
            DrawShowFlagItem("Skeleton", showFlags.ShowDebugSkeleton);
            ImGui::Separator();
            DrawShowFlagItem("Wireframe", showFlags.ShowWireframe);
            if (settings != nullptr)
            {
                DrawWireframeOptions(*settings, showFlags.ShowWireframe);
            }
        }

        inline void TextCentered(std::string text)
        {
            auto windowWidth = ImGui::GetContentRegionAvail().x;
            auto textWidth   = ImGui::CalcTextSize(text.c_str()).x;

            ImGui::SetCursorPosX((ImGui::GetCursorPosX() + windowWidth - textWidth) * 0.5f);
            ImGui::TextUnformatted(text.c_str());
        }

        inline ImVec2 TextCentered(std::string text, float FixedWidth)
        {
            auto textWidth   = ImGui::CalcTextSize(text.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (FixedWidth - textWidth) * 0.5f);
            ImVec2 drawPos = ImGui::GetCursorScreenPos() + ImVec2(textWidth * 0.5f, ImGui::GetTextLineHeight() * 0.5f);
            ImGui::TextUnformatted(text.c_str());
            return drawPos;
        }

        inline ImU32 Vec4ToImU32(const glm::vec4& color)
        {
            return IM_COL32(
                static_cast<int>(color.r * 255.0f),
                static_cast<int>(color.g * 255.0f),
                static_cast<int>(color.b * 255.0f),
                static_cast<int>(color.a * 255.0f));
        }
    }
}

#define BUTTON_TOOLTIP( text ) if (ImGui::IsItemHovered()) {\
ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8,8));\
ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4);\
ImGui::BeginTooltip(); ImGui::TextUnformatted( text ); ImGui::EndTooltip();\
ImGui::PopStyleVar(2);\
}
