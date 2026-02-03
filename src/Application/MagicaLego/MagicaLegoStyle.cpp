#include "MagicaLegoStyle.hpp"
#include <imgui.h>

namespace MagicaLego::Style
{
    void ApplyStyle()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        
        style.WindowRounding = WindowRounding;
        style.FrameRounding = FrameRounding;
        style.PopupRounding = PopupRounding;
        style.ScrollbarRounding = 9.0f;
        style.GrabRounding = FrameRounding;
        style.TabRounding = FrameRounding;
        
        style.Colors[ImGuiCol_WindowBg] = GlassBackground;
        style.Colors[ImGuiCol_TitleBg] = TireGray;
        style.Colors[ImGuiCol_TitleBgActive] = TireGray;
        style.Colors[ImGuiCol_TitleBgCollapsed] = TireGray;
        
        // Buttons
        style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 0.4f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(LegoYellow.x, LegoYellow.y, LegoYellow.z, 0.5f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(LegoYellow.x, LegoYellow.y, LegoYellow.z, 0.8f);
        
        // Frame
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.3f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.4f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.3f, 0.3f, 0.3f, 0.5f);
        
        // Text
        style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
        
        // Header
        style.Colors[ImGuiCol_Header] = ImVec4(LegoYellow.x, LegoYellow.y, LegoYellow.z, 0.3f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(LegoYellow.x, LegoYellow.y, LegoYellow.z, 0.6f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(LegoYellow.x, LegoYellow.y, LegoYellow.z, 0.8f);
    }
}
