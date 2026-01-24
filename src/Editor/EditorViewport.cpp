#include "EditorGUI.h"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Runtime/UserInterface.hpp"
#include "Assets/Model.hpp"
#include "Runtime/Engine.hpp"
#include "Utilities/ImGui.hpp"
#include "Utilities/Localization.hpp"
#include "Utilities/Math.hpp"
#include <fmt/format.h>

const float toolIconWidth = 32.0f;

void Editor::GUI::ShowViewport(ImGuiID id)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiDockNode* node = ImGui::DockBuilderGetCentralNode(id);

    ImGui::SetNextWindowPos(node->Pos + ImVec2(5,5));
    ImGui::SetNextWindowSize(ImVec2(160, 140));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));

    ImGuiWindowFlags windowFlags = 0
        | ImGuiWindowFlags_NoDocking
        | ImGuiWindowFlags_NoTitleBar
        | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoScrollbar
        | ImGuiWindowFlags_NoSavedSettings
        ;

    ImGui::Begin("ViewportStat", nullptr, windowFlags);

    ImGui::Text("Reatime Statstics: ");
    ImGui::Text("Frame rate: %.0f fps", 1.0f / engine->GetSmoothDeltaSeconds());
    ImGui::Text("Progressive: %d", engine->IsProgressiveRendering());

    auto& gpuDrivenStat = current_scene->GetGpuDrivenStat();
    uint32_t instanceCount = gpuDrivenStat.ProcessedCount - gpuDrivenStat.CulledCount;
    uint32_t triangleCount = gpuDrivenStat.TriangleCount - gpuDrivenStat.CulledTriangleCount;
    ImGui::Text("Tris: %s/%s", Utilities::metricFormatter(static_cast<double>(triangleCount), "").c_str(), Utilities::metricFormatter(static_cast<double>(gpuDrivenStat.TriangleCount), "").c_str());
    ImGui::Text("Draw: %s/%s", Utilities::metricFormatter(static_cast<double>(instanceCount), "").c_str(), Utilities::metricFormatter(static_cast<double>(gpuDrivenStat.ProcessedCount), "").c_str());

    ImGui::End();
        
    ImGui::SetNextWindowPos(node->Pos + ImVec2(170,0));
    ImGui::SetNextWindowSize(ImVec2(node->Size.x - 170, toolIconWidth + 8));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0);
    
    ImGui::Begin("ViewportTool", nullptr, windowFlags);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    
    ImGui::Dummy(ImVec2(node->Size.x - 170 - (toolIconWidth + 16) * 3, 0)); 
    ImGui::SameLine(); 
    if (ImGui::Button(ICON_FA_EYE, ImVec2(toolIconWidth, toolIconWidth)))
    {
        ImGui::OpenPopup("ViewportShowFlags");
    }
    BUTTON_TOOLTIP(LOCTEXT("Show Flags"))
    
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 12));
    if (ImGui::BeginPopup("ViewportShowFlags"))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 10));
        auto& showFlags = engine->GetShowFlags();
        Utilities::UI::DrawShowFlagsCommon(showFlags);
        
        ImGui::PopStyleVar();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
       
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    
    ImGui::End();
}
