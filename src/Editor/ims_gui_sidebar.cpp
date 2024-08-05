#include "IconsFontAwesome6.h"
#include "ims_gui.h"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"

void ImStudio::GUI::ShowSidebar(const Assets::Scene* scene)
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
            ImGuiTreeNodeFlags flag = ImGuiTreeNodeFlags_Leaf;
            
            ImGui::PushStyleColor(ImGuiCol_HeaderActive , ImVec4(0.3f, 0.3f, 0.9f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
            if( limit % 2 == 0 ) { flag |= ImGuiTreeNodeFlags_Selected; ImGui::PushStyleColor(ImGuiCol_Header , ImVec4(0.0f, 0.0f, 0.0f, 0.1f));}
            bool selected = (selected_obj == &node);
            if(selected)
            {
                flag |= ImGuiTreeNodeFlags_Selected;
                ImGui::PushStyleColor(ImGuiCol_Header , ImVec4(0.2f, 0.2f, 0.8f, 1.0f));
            }
            if (ImGui::TreeNodeEx((ICON_FA_CUBE " " + node.GetName()).c_str(), flag))
            {
                if (ImGui::IsItemClicked())
                {
                    selected_obj = &node;
                }
                ImGui::TreePop();
            }
            if(selected)
            {
                ImGui::PopStyleColor();
            }
            if( limit % 2 == 0 ) ImGui::PopStyleColor();
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
