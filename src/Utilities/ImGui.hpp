#pragma once
#include <imgui.h>
#include <string>

namespace Utilities
{
    namespace UI
    {
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
