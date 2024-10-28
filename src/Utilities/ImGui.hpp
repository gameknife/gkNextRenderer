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

        inline void TextCentered(std::string text, int FixedWidth)
        {
            auto textWidth   = ImGui::CalcTextSize(text.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (FixedWidth - textWidth) * 0.5f);
            ImGui::TextUnformatted(text.c_str());
        }   
    }
}
