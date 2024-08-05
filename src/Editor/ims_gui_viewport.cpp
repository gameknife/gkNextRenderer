#include "ims_gui.h"
#include "Assets/Model.hpp"

void ImStudio::GUI::ShowViewport(ImGuiID id)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiDockNode* node = ImGui::DockBuilderGetCentralNode(id);

    ImGui::SetNextWindowPos(node->Pos);
    ImGui::SetNextWindowSize(node->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0);

    ImGuiWindowFlags window_flags = 0
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoSavedSettings
        ;

    ImGui::Begin("ViewportStat", nullptr, window_flags);

    ImGui::Text("Reatime Statstics: ");

    ImGui::End();
}
