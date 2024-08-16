#include "EditorGUI.h"
#include "ImNodeFlow.h"
#include "Editor/Nodes/NodeSetInt.hpp"
#include "Editor/Nodes/NodeSetFloat.hpp"
#include "Nodes/NodeMaterial.hpp"

void Editor::GUI::ShowMaterialEditor()
{
    static ImFlow::ImNodeFlow myNode;
    static bool init_nodes = true;
    static std::shared_ptr<Nodes::NodeSetColor> ptNodeTemperature;
    static std::shared_ptr<Nodes::NodeSetFloat> ptNodeSetpoint;
    static std::shared_ptr<Nodes::NodeMaterial> ptNodePlot;

    if(init_nodes)
    {
        ImGui::SetNextWindowSize(ImVec2(1280,800));
    }
    ImGui::Begin("Material Editor", &ed_material, ImGuiWindowFlags_NoDocking);

    
    if (init_nodes)
    {
        ptNodeTemperature = myNode.placeNodeAt<Nodes::NodeSetColor>(ImVec2(30, 20), "Albedo", glm::vec3(1,1,1));
        ptNodeSetpoint = myNode.placeNodeAt<Nodes::NodeSetFloat>(ImVec2(30, 210), "Roughness", 1.0);
        ptNodePlot = myNode.placeNodeAt<Nodes::NodeMaterial>(ImVec2(300, 180));
        
        ptNodeTemperature->outPin("Out")->createLink(ptNodePlot->inPin("Albedo"));
        ptNodeSetpoint->outPin("Out")->createLink(ptNodePlot->inPin("Roughness"));

        init_nodes = false;
    }

    myNode.rightClickPopUpContent([](ImFlow::BaseNode *node)
                                      {
            if (node != nullptr)
            {
                ImGui::Text("%lu", node->getUID());
                ImGui::Text("%s", node->getName().c_str());
                if (ImGui::Button("Ok"))
                {
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::Button("Delete Node"))
                {
                    node->destroy();
                    ImGui::CloseCurrentPopup();
                }
            }
            else
            {
                if (ImGui::Button("Add Node"))
                {
                    ImVec2 pos = ImGui::GetMousePos();
                    myNode.placeNodeAt<Nodes::NodeSetFloat>(pos, "Test", 1);
                    ImGui::CloseCurrentPopup();
                }
            } });

    myNode.droppedLinkPopUpContent([](ImFlow::Pin *dragged)
                                   { dragged->deleteLink(); });

    std::vector<std::weak_ptr<ImFlow::Link>> myLinks = myNode.getLinks();

    ImGui::Text("Links: %lu", myLinks.size());
    ImGui::SameLine();
    ImGui::Text("Nodes: %u", myNode.getNodesCount());

    for (auto wp : myLinks)
    {
        auto p = wp.lock();
        if (p == nullptr)
            continue;
        if (p->isHovered())
        {
            // ImGui::Text("Hovered");
        }
        if (p->isSelected() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            // ImGui::Text("Selected and Right Mouse click");
            ImFlow::Pin *right = p->right();
            ImFlow::Pin *left = p->left();
            right->deleteLink();
            left->deleteLink();
        }
    }

    myNode.update();
    ImGui::End();
}
