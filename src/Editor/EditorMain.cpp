#include "EditorMain.h"
#include <imgui_internal.h>

#include "UserInterface.hpp"

void MainWindowStyle()
{
    
    ImGuiIO &io = ImGui::GetIO();

    io.IniFilename              = NULL;

    ImGuiStyle* style = &ImGui::GetStyle();
    ImVec4* colors = style->Colors;
    ImGui::StyleColorsDark(style);
    colors[ImGuiCol_Text]                   = ImVec4(0.84f, 0.84f, 0.84f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.09f, 0.09f, 0.09f, 1.00f);
    colors[ImGuiCol_Border]                 = ImVec4(0.17f, 0.17f, 0.17f, 1.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.10f, 0.10f, 0.10f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.47f, 0.47f, 0.47f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.33f, 0.33f, 0.33f, 1.00f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.27f, 0.27f, 0.27f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.54f, 0.54f, 0.54f, 1.00f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.19f, 0.39f, 0.69f, 1.00f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.19f, 0.19f, 0.19f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.20f, 0.39f, 0.69f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.28f, 0.45f, 0.70f, 1.00f);
    style->WindowPadding                    = ImVec2(12.00f, 8.00f);
    //style->ItemSpacing                      = ImVec2(7.00f, 6.00f);
    style->GrabMinSize                      = 20.00f;
    style->WindowRounding                   = 8.00f;
    style->FrameBorderSize                  = 0.00f;
    style->FrameRounding                    = 4.00f;
    style->GrabRounding                     = 12.00f;
}

void MainWindowGUI(Editor::GUI & gui_r, const Assets::Scene* scene, const Statistics& statistics, ImGuiID id, bool firstRun)
{
    //////////////////////////////////
    Editor::GUI &gui = gui_r;
    ImGuiIO &io = ImGui::GetIO();

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
    }

}