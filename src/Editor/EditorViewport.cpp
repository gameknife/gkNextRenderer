#include "EditorGUI.h"
#include "UserInterface.hpp"
#include "Assets/Model.hpp"
#include "Utilities/Math.hpp"

void Editor::GUI::ShowViewport(ImGuiID id, const Statistics& statistics)
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
        | ImGuiWindowFlags_NoInputs
        ;

    ImGui::Begin("ViewportStat", nullptr, window_flags);

    ImGui::Text("Reatime Statstics: ");
    ImGui::Text("Frame rate: %.0f fps", statistics.FrameRate);
    ImGui::Text("Campos:  %.2f %.2f %.2f", statistics.CamPosX, statistics.CamPosY, statistics.CamPosZ);

    ImGui::Text("Tris: %s", Utilities::metricFormatter(static_cast<double>(statistics.TriCount), "").c_str());
    ImGui::Text("Instance: %s", Utilities::metricFormatter(static_cast<double>(statistics.InstanceCount), "").c_str());
    ImGui::Text("Texture: %d", statistics.TextureCount);

    ImGui::End();
}
