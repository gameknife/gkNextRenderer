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

        // if (ImGui::Button("Input Text"))
        //     bw.create("textinput");
        //
        // if (ImGui::Button("Input Int"))
        //     bw.create("inputint");
        //
        // ImGui::SameLine(); utils::HelpMarker
        // ("You can apply arithmetic operators +,*,/ on numerical values.\n"
        // "  e.g. [ 100 ], input \'*2\', result becomes [ 200 ]\n"
        // "Use +- to subtract.");
        //
        // if (ImGui::Button("Input Float"))
        //     bw.create("inputfloat");
        //
        // if (ImGui::Button("Input Double"))
        //     bw.create("inputdouble");
        //
        // if (ImGui::Button("Input Scientific"))
        //     bw.create("inputscientific");
        //
        // ImGui::SameLine(); utils::HelpMarker
        // ("You can input value using the scientific notation,\n"
        // "  e.g. \"1e+8\" becomes \"100000000\".");
        //
        // if (ImGui::Button("Input Float3"))
        //     bw.create("inputfloat3");
        //
        // if (ImGui::Button("Drag Int"))
        //     bw.create("dragint");
        //
        // ImGui::SameLine(); utils::HelpMarker
        // ("Click and drag to edit value.\n"
        // "Hold SHIFT/ALT for faster/slower edit.\n"
        // "Double-click or CTRL+click to input value.");
        //
        // if (ImGui::Button("Drag Int %"))
        //     bw.create("dragint100");
        //
        // if (ImGui::Button("Drag Float"))
        //     bw.create("dragfloat");
        //
        // if (ImGui::Button("Drag Float Small"))
        //     bw.create("dragfloatsmall");
        //
        // if (ImGui::Button("Slider Int"))
        //     bw.create("sliderint");
        //
        // ImGui::SameLine(); utils::HelpMarker("CTRL+click to input value.");
        //
        // if (ImGui::Button("Slider Float"))
        //     bw.create("sliderfloat");
        //
        // if (ImGui::Button("Slider Float Log"))
        //     bw.create("sliderfloatlog");
        //
        // if (ImGui::Button("Slider Angle"))
        //     bw.create("sliderangle");

        if ((ImGui::GetIO().KeyAlt) && (ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_F4))))
        {
            state = false;
        }
        
    }

    ImGui::End();
    ImGui::PopStyleVar(1);
}
