#include "IconsFontAwesome6.h"
#include "ims_gui.h"

void ImStudio::GUI::ShowMenubar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    //ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    // MENU
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 55, viewport->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - 255, 55));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0);
    
    ImGui::Begin("Menubar", NULL,
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::GetWindowDrawList()->AddRectFilled(viewport->Pos, viewport->Pos + ImVec2(viewport->Size.x, 55), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    ImGui::PopStyleVar();
    
    if (ImGui::BeginMenuBar())
    {
        /// menu-file
        if (ImGui::BeginMenu("File"))
        {
            #ifndef __EMSCRIPTEN__
            if (ImGui::MenuItem("Export to clipboard"))
            {
                ImGui::LogToClipboard();
                ImGui::LogText("%s", output.c_str());
                ImGui::LogFinish();
            };
            #endif

            if (ImGui::MenuItem("Exit"))
            {
                state = false;
            };
            ImGui::EndMenu();
        }

        /// menu-edit
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::BeginMenu("Layout"))
            {
                ImGui::MenuItem("Compact", NULL, &compact);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Behavior"))
            {
                ImGui::MenuItem("Static Mode", NULL, &bw.staticlayout);
                ImGui::SameLine();
                utils::HelpMarker("Toggle between static/linear layout and fixed/manual layout");

                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reset"))
            {
                bw.objects.clear();
                bw.selected_obj_id = -1;
                bw.open_child_id = -1;
                bw.open_child = false;
                bw.idgen = 0;
            }

            ImGui::EndMenu();
        }

        /// menu-tools
        if (ImGui::BeginMenu("Tools"))
        {
            ImGui::MenuItem("Style Editor", NULL, &child_style);
            ImGui::MenuItem("Demo Window", NULL, &child_demo);
            ImGui::MenuItem("Metrics", NULL, &child_metrics);
            ImGui::MenuItem("Stack Tool", NULL, &child_stack);
            ImGui::MenuItem("Color Export", NULL, &child_color);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Resources")) child_resources = true;
            if (ImGui::MenuItem("About ImStudio")) child_about = true;
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }

    // TAB
    // if (!compact)
    // {
    //     if (ImGui::BeginTabBar("##Tabs", ImGuiTabBarFlags_None))
    //     {
    //         // tab-create
    //         if (ImGui::BeginTabItem("Create"))
    //         {
    //             wksp_output = false;
    //             wksp_create = true;
    //             ImGui::EndTabItem();
    //         }
    //
    //         // tab-output
    //         if (ImGui::BeginTabItem("Output"))
    //         {
    //             wksp_create = false;
    //             wksp_output = true;
    //             ImGui::EndTabItem();
    //         }
    //
    //         ImGui::EndTabBar();
    //     }
    // }
    
    ImGui::End();

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(55, 55));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    
    ImGui::Begin("Logo", NULL,
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking);

    ImGui::GetWindowDrawList()->AddRectFilled(viewport->Pos, viewport->Pos + ImVec2(55, 55), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    ImGui::PushFont(bigIcon_);
    ImGui::GetWindowDrawList()->AddText( viewport->Pos + ImVec2(10,7), IM_COL32(110,240,60,255), ICON_FA_SEEDLING);
    ImGui::PopFont();
    ImGui::End();

    ImGui::SetNextWindowPos(viewport->Pos + ImVec2(viewport->Size.x - 200, 0));
    ImGui::SetNextWindowSize(ImVec2(200, 55));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0);

    ImGui::Begin("XMark", NULL,
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking);
    
    ImGui::GetWindowDrawList()->AddRectFilled(viewport->Pos + ImVec2(viewport->Size.x - 200, 0), viewport->Pos + ImVec2(viewport->Size.x, 55), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    ImGui::SetCursorPos( ImVec2(50,5) );
    ImGui::PushStyleColor( ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    ImGui::Button(ICON_FA_WINDOW_MINIMIZE, ImVec2(40, 40));ImGui::SameLine();
    ImGui::Button(ICON_FA_WINDOW_MAXIMIZE, ImVec2(40, 40));ImGui::SameLine();
    ImGui::Button(ICON_FA_XMARK, ImVec2(40, 40));ImGui::SameLine();
    ImGui::PopStyleColor();
    ImGui::End();
    
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
}