#include "EditorCommand.hpp"
#include "IconsFontAwesome6.h"
#include "EditorGUI.h"

void Editor::GUI::ShowMenubar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
    // MENU
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 55, viewport->Pos.y));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - 255, 55));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Menubar", NULL,
                  ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking);


    ImGui::GetWindowDrawList()->AddRectFilled(viewport->Pos, viewport->Pos + ImVec2(viewport->Size.x, 55), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    ImGui::PopStyleVar();
    
    if (ImGui::BeginMenuBar())
    {
        /// menu-file
        if (ImGui::BeginMenu("File"))
        {
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
                ImGui::MenuItem("Reset");
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Behavior"))
            {
                ImGui::MenuItem("Static Mode", NULL);
                ImGui::SameLine();
                utils::HelpMarker("Toggle between static/linear layout and fixed/manual layout");

                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reset"))
            {
               
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
            ImGui::MenuItem("Material Editor", NULL, &child_mat_editor);
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
    
    ImGui::End();

    // LOGO
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
    ImGui::GetWindowDrawList()->AddText( viewport->Pos + ImVec2(10,7), IM_COL32(240,180,60,255), ICON_FA_SHEKEL_SIGN);
    ImGui::PopFont();
    ImGui::End();

    // XMARK
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
    if(ImGui::Button(ICON_FA_WINDOW_MINIMIZE, ImVec2(40, 40))) {
        EditorCommand::ExecuteCommand(EEditorCommand::ECmdSystem_RequestMinimize);
    };ImGui::SameLine();
    if(ImGui::Button(ICON_FA_WINDOW_MAXIMIZE, ImVec2(40, 40))) {
        EditorCommand::ExecuteCommand(EEditorCommand::ECmdSystem_RequestMaximum);
    };ImGui::SameLine();
    if(ImGui::Button(ICON_FA_XMARK, ImVec2(40, 40))) {
        EditorCommand::ExecuteCommand(EEditorCommand::ECmdSystem_RequestExit);
    };ImGui::SameLine();
    ImGui::PopStyleColor();
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    
    // FOOTER
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - 40));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, 40));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(100);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    
    ImGui::Begin("Footer", NULL,
              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking);

    ImGui::GetWindowDrawList()->AddLine(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - 40), ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y - 40), IM_COL32(20,20,20,255), 2);
    
    if(ImGui::Button(ICON_FA_HOUSE, ImVec2(60, 30))) {

    };ImGui::SameLine();
    if(ImGui::Button(ICON_FA_PEN, ImVec2(60, 30))) {

    };ImGui::SameLine();
    char cvar[255] = "";
    ImGui::SetNextItemWidth(200);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6,6));
    ImGui::InputTextWithHint("##CVar", "Execute CVar...", cvar, 255);
    ImGui::PopStyleVar();
    ImGui::End();

    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
}