#include "IconsFontAwesome6.h"
#include "EditorGUI.h"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"

void Editor::GUI::ShowSidebar(const Assets::Scene* scene)
{
    //ImGui::SetNextWindowPos(sb_P);
    //ImGui::SetNextWindowSizeConstraints(ImVec2(0, -1), ImVec2(FLT_MAX, -1));
    //ImGui::SetNextWindowSize(sb_S);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.00f, 5.00f));
    ImGui::Begin("Outliner", NULL);

    /// content-sidebar
    {

        ImGui::TextDisabled("NOTE");
        ImGui::SameLine(); utils::HelpMarker
        ("ALL SCENE NODES\n"
        "limited to 1000 nodes\n"
        "select and view node properties\n");
        ImGui::Separator();
        
        //ANCHOR SIDEBAR.DATAINPUTS
        ImGui::Text("Nodes");
        ImGui::Separator();
        
        auto& allnodes = scene->Nodes();
        uint32_t limit = 1000;
        for( auto& node : allnodes )
        {
            

            // draw stripe background
            ImVec2 WindowPadding = ImGui::GetStyle().WindowPadding;
            ImVec2 CursorPos = ImGui::GetCursorScreenPos() - ImVec2(WindowPadding.x,2);
            ImGui::GetWindowDrawList()->AddRectFilled(CursorPos, CursorPos + ImVec2(ImGui::GetWindowSize().x - WindowPadding.x * 2, 22),limit % 2 == 0 ? IM_COL32(0, 0, 0, 50) : IM_COL32(0, 0, 0, 0), 0);

            ImGuiTreeNodeFlags flag = ImGuiTreeNodeFlags_Leaf;
            
            ImGui::PushStyleColor(ImGuiCol_HeaderActive , ImVec4(0.3f, 0.3f, 0.9f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

            if(selected_obj == &node)
            {
                ImGui::GetWindowDrawList()->AddRectFilled(CursorPos, CursorPos + ImVec2(ImGui::GetWindowSize().x - WindowPadding.x * 2, 22),IM_COL32(70, 120, 255, 255), 0);
            }
            
            if (ImGui::TreeNodeEx((ICON_FA_CUBE " " + node.GetName()).c_str(), flag))
            {
                if (ImGui::IsItemClicked())
                {
                    selected_obj = &node;
                }
                ImGui::TreePop();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            if( limit-- <= 0 )
            {
                break;
            }
        }

        if ((ImGui::GetIO().KeyAlt) && (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F4))))
        {
            state = false;
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(1);
}
