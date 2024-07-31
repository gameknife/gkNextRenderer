#include "main_window.h"
#include <imgui_internal.h>

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
    style->ItemSpacing                      = ImVec2(7.00f, 3.00f);
    style->GrabMinSize                      = 20.00f;
    style->WindowRounding                   = 8.00f;
    style->FrameBorderSize                  = 0.00f;
    style->FrameRounding                    = 4.00f;
    style->GrabRounding                     = 12.00f;
    
}

void MainWindowGUI(ImStudio::GUI & gui_r, ImTextureID viewportImage, ImVec2 viewportSize, bool firstRun)
{
     ImGuiID id = ImGui::DockSpaceOverViewport(0, ImGuiDockNodeFlags_PassthruCentralNode, 0);
    
    //////////////////////////////////
    ImStudio::GUI &gui = gui_r;
    ImGuiIO &io = ImGui::GetIO();

    // static int w_w = io.DisplaySize.x;
    // static int w_h = io.DisplaySize.y;
    int w_w = io.DisplaySize.x;
    int w_h = io.DisplaySize.y;
    //////////////////////////////////

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::SetNextWindowBgAlpha(0.00f);

    // window-menubar
    gui.mb_P = ImVec2(0, 0);
    gui.mb_S = ImVec2(w_w, 46);
    if (gui.menubar) gui.ShowMenubar();

// Only run DockBuilder functions on the first frame of the app:
if (firstRun) {
    // 2. We want our whole dock node to be positioned in the center of the window, so we'll need to calculate that first.
    // The "work area" is the space inside the platform window created by GLFW, SDL, etc. minus the main menu bar if present.
    ImVec2 workPos = ImGui::GetMainViewport()->WorkPos;   // The coordinates of the top-left corner of the work area
    ImVec2 workSize = ImGui::GetMainViewport()->WorkSize; // The dimensions (size) of the work area

    // We'll need to halve the size, then add those resultant values to the top-left corner to get to the middle point on both X and Y.
    ImVec2 workCenter{ workPos.x + workSize.x * 0.5f, workPos.y + workSize.y * 0.5f };

    // For completeness, this can be simplified in modern versions of Dear ImGui.
    // v1.81 (found by git blame) adds a new function GetWorkCenter() which does these same calculations, so for any code using a newer version:
    //
    // if (firstLoop) {
    //     ImVec2 workCenter = ImGui::GetMainViewport()->GetWorkCenter();

    // 3. Now we'll need to create our dock node:
    //ImGuiID id = ImGui::GetID("MainWindowGroup"); // The string chosen here is arbitrary (it just gives us something to work with)
    //ImGui::DockBuilderRemoveNode(id);             // Clear any preexisting layouts associated with the ID we just chose
    //ImGui::DockBuilderAddNode(id);                // Create a new dock node to use

    // 4. Set the dock node's size and position:
    ImVec2 size{ 1280, 720 }; // A decently large dock node size (600x300px) so everything displays clearly

    // Calculate the position of the dock node
    //
    // `DockBuilderSetNodePos()` will position the top-left corner of the node to the coordinate given.
    // This means that the node itself won't actually be in the center of the window; its top-left corner will.
    //
    // To fix this, we'll need to subtract half the node size from both the X and Y dimensions to move it left and up.
    // This new coordinate will be the position of the node's top-left corner that will center the node in the window.
    ImVec2 nodePos{ workCenter.x - size.x * 0.5f, workCenter.y - size.y * 0.5f };

    // Set the size and position:
    //ImGui::DockBuilderSetNodeSize(id, size);
    //ImGui::DockBuilderSetNodePos(id, nodePos);

    // 5. Split the dock node to create spaces to put our windows in:

    // Split the dock node in the left direction to create our first docking space. This will be on the left side of the node.
    // (The 0.5f means that the new space will take up 50% of its parent - the dock node.)
    ImGuiID dock1 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.25f, nullptr, &id);
    // +-----------+
    // |           |
    // |     1     |
    // |           |
    // +-----------+

    // Split the same dock node in the right direction to create our second docking space.
    // At this point, the dock node has two spaces, one on the left and one on the right.
    ImGuiID dock2 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Right, 0.25f, nullptr, &id);
    // +-----+-----+
    // |     |     |
    // |  1  |  2  |
    // |     |     |
    // +-----+-----+
    //    split ->
    
    // For our last docking space, we want it to be under the second one but not under the first.
    // Split the second space in the down direction so that we now have an additional space under it.
    //
    // Notice how "dock2" is now passed rather than "id".
    // The new space takes up 50% of the second space rather than 50% of the original dock node.
    ImGuiID dock3 = ImGui::DockBuilderSplitNode(id, ImGuiDir_Down, 0.25f, nullptr, &id);
    // +-----+-----+
    // |     |  2  |  split
    // |  1  +-----+    |
    // |     |  3  |    V
    // +-----+-----+

    // 6. Add windows to each docking space:
    ImGui::DockBuilderDockWindow("Sidebar", dock1);
    ImGui::DockBuilderDockWindow("Properties", dock2);
    ImGui::DockBuilderDockWindow("Output", dock3);

    // 7. We're done setting up our docking configuration:
    ImGui::DockBuilderFinish(id);
}
    
    // workspace-create
    //if (!gui.compact){
    if(false)
    {
        if (gui.wksp_create)
        {// create-main
            {// create-sidebar
                gui.sb_P = ImVec2(0, gui.mb_S.y);
                gui.sb_S = ImVec2(170, w_h - gui.mb_S.y);
                if (gui.sidebar) gui.ShowSidebar();

                // create-properties
                gui.pt_P = ImVec2(w_w - 300, gui.mb_S.y);
                gui.pt_S = ImVec2(300, w_h - gui.mb_S.y);
                if (gui.properties) gui.ShowProperties();

                // create-viewport
                gui.vp_P = ImVec2(gui.sb_S.x, gui.mb_S.y);
                gui.vp_S = ImVec2(gui.pt_P.x - gui.sb_S.x, w_h - gui.mb_S.y);
                if (gui.viewport) gui.ShowViewport(viewportImage, viewportSize);

            }
        }
        // workspace-output
        gui.ot_P = ImVec2(0, gui.mb_S.y);
        gui.ot_S = ImVec2(w_w, w_h - gui.mb_S.y);
        if (gui.wksp_output) gui.ShowOutputWorkspace();
    }
    else
    {
        gui.wksp_output = true;
            
        // create-sidebar
        gui.sb_P = ImVec2(0, gui.mb_S.y);
        gui.sb_S = ImVec2(170, w_h - gui.mb_S.y);
        if (gui.sidebar) gui.ShowSidebar();

        // create-properties
        gui.pt_P = ImVec2(w_w - 300, gui.mb_S.y);
        gui.pt_S = ImVec2(300, w_h - gui.mb_S.y);
        if (gui.properties) gui.ShowProperties();

        // workspace-output
        gui.ot_P = ImVec2(gui.sb_S.x, w_h - 300);
        gui.ot_S = ImVec2(gui.pt_P.x - gui.sb_S.x, 300);
        if (gui.wksp_output) gui.ShowOutputWorkspace();

        // create-viewport
        gui.vp_P = ImVec2(gui.sb_S.x, gui.mb_S.y);
        gui.vp_S = ImVec2(gui.pt_P.x - gui.sb_S.x, w_h - gui.mb_S.y);
        //if (gui.viewport) gui.ShowViewport(viewportImage, viewportSize);
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