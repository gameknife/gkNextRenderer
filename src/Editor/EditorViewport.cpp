#include "EditorGUI.h"
#include "IconsFontAwesome6.h"
#include "Runtime/UserInterface.hpp"
#include "Assets/Model.hpp"
#include "Runtime/Engine.hpp"
#include "Utilities/ImGui.hpp"
#include "Utilities/Localization.hpp"
#include "Utilities/Math.hpp"

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
    
    ImGui::Begin("ViewportTool", nullptr, window_flags);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
    
    ImGui::Dummy(ImVec2(node->Size.x - 170 - (toolIconWidth + 16) * 3, 0)); 
    ImGui::SameLine(); if (ImGui::Button(ICON_FA_LINE, ImVec2(toolIconWidth, toolIconWidth)) )
    {
        engine->GetUserSettings().DebugDraw_Lighting = !engine->GetUserSettings().DebugDraw_Lighting;
    }
    BUTTON_TOOLTIP(LOCTEXT("Toggle Lighting"))
    ImGui::SameLine(); if ( ImGui::Button(ICON_FA_SOAP, ImVec2(toolIconWidth, toolIconWidth)))
    {
        engine->GetUserSettings().ShowVisualDebug = !engine->GetUserSettings().ShowVisualDebug;
    }
    BUTTON_TOOLTIP(LOCTEXT("Toggle VisualDebug"))
    ImGui::SameLine(); if ( ImGui::Button(ICON_FA_DRAW_POLYGON, ImVec2(toolIconWidth, toolIconWidth)))
    {
        engine->GetRenderer().showWireframe_ = !engine->GetRenderer().showWireframe_;
    }
    BUTTON_TOOLTIP(LOCTEXT("Toggle Wireframe"))
       
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    
    ImGui::End();
}
