#include "EditorMain.h"
#include <imgui_internal.h>

void MainWindowGUI(Editor::GUI & gui_r, Assets::Scene* scene, const Statistics& statistics, ImGuiID id, bool firstRun)
{
    //////////////////////////////////
    Editor::GUI &gui = gui_r;

    gui.current_scene = scene;
    
    // Only run DockBuilder functions on the first frame of the app:
    if (firstRun) {
        ImGuiID dock1 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.1f, nullptr, &id);
        ImGuiID dock2 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.2f, nullptr, &id);
        ImGuiID dock3 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Down, 0.25f, nullptr, &id);

        ImGui::DockBuilderDockWindow("Outliner", dock1);
        ImGui::DockBuilderDockWindow("Properties", dock2);
        ImGui::DockBuilderDockWindow("Content Browser", dock3);

        ImGui::DockBuilderFinish(id);
    }

    {
        gui.ShowMenubar();

        // create-sidebar
        if (gui.sidebar) gui.ShowSidebar(scene);

        // create-properties
        if (gui.properties) gui.ShowProperties();

        // workspace-output
        if (gui.contentBrowser) gui.ShowContentBrowser();

        // create-viewport
        if (gui.viewport) gui.ShowViewport(id, statistics);
    }

    { // create-children
        if (gui.child_style) utils::ShowStyleEditorWindow(&gui.child_style);
        if (gui.child_demo) ImGui::ShowDemoWindow(&gui.child_demo);
        if (gui.child_metrics) ImGui::ShowMetricsWindow(&gui.child_metrics);
        if (gui.child_stack) ImGui::ShowStackToolWindow(&gui.child_stack);
        if (gui.child_color) utils::ShowColorExportWindow(&gui.child_color);
        if (gui.child_resources) utils::ShowResourcesWindow(&gui.child_resources);
        if (gui.child_about) utils::ShowAboutWindow(&gui.child_about);
        //if (gui.child_mat_editor) utils::ShowDemoMaterialEditor(&gui.child_mat_editor);
    }

    {
        if(gui.ed_material) gui.ShowMaterialEditor();
    }

}