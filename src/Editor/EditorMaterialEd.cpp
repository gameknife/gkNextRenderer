#include "EditorGUI.h"
#include "ImNodeFlow.h"
#include "Editor/Nodes/NodeSetInt.hpp"
#include "Editor/Nodes/NodeSetFloat.hpp"
#include "Nodes/NodeMaterial.hpp"
#include "Assets/Material.hpp"
#include "Assets/Scene.hpp"

static std::unique_ptr<ImFlow::ImNodeFlow> myNode;
static std::weak_ptr<Nodes::NodeMaterial> matNode;
static bool init_nodes = true;

void Editor::GUI::OpenMaterialEditor()
{
    myNode.reset( new ImFlow::ImNodeFlow() );
    init_nodes = true;
    
    if(selected_material != nullptr)
    {
        float baseY = 20.0f;
        float seprateY = 100.0f;
        float seprateX = 400.0f;
        // from material to node
        auto nodeIOR = myNode->placeNodeAt<Nodes::NodeSetFloat>(ImVec2(30,baseY), "IOR", selected_material->RefractionIndex);
        auto nodeShadingMode = myNode->placeNodeAt<Nodes::NodeSetInt>(ImVec2(30,baseY += seprateY), "ShadingMode", (int)selected_material->MaterialModel);
        
        auto nodeAlbedo = myNode->placeNodeAt<Nodes::NodeSetColor>(ImVec2(30, baseY += seprateY), "Albedo", glm::vec3(selected_material->Diffuse));
        auto nodeRoughness  = myNode->placeNodeAt<Nodes::NodeSetFloat>(ImVec2(30, baseY += seprateY), "Roughness", selected_material->Fuzziness);
        auto nodeMetalness  = myNode->placeNodeAt<Nodes::NodeSetFloat>(ImVec2(30, baseY += seprateY), "Metalness", selected_material->Metalness);


        
        auto nodeMat  = myNode->placeNodeAt<Nodes::NodeMaterial>(ImVec2(seprateX, baseY * 0.5f - seprateY));
        matNode = nodeMat;

        nodeIOR->outPin("Out")->createLink(nodeMat->inPin("IOR"));
        nodeShadingMode->outPin("Out")->createLink(nodeMat->inPin("ShadingMode"));
        
        nodeAlbedo->outPin("Out")->createLink(nodeMat->inPin("Albedo"));
        nodeRoughness->outPin("Out")->createLink(nodeMat->inPin("Roughness"));
        nodeMetalness->outPin("Out")->createLink(nodeMat->inPin("Metalness"));

        if(selected_material->DiffuseTextureId != -1)
        {
            auto nodeAlbedoTexture = myNode->placeNodeAt<Nodes::NodeSetTexture>(ImVec2(30,baseY += seprateY), "AlbedoTexture", selected_material->DiffuseTextureId);
            nodeAlbedoTexture->outPin("Out")->createLink(nodeMat->inPin("AlbedoTexture"));
        }
    }
}

void Editor::GUI::ApplyMaterial()
{
    if(std::shared_ptr<Nodes::NodeMaterial> mat = matNode.lock())
    {
        const glm::vec3& color = mat->getInVal<glm::vec3>("Albedo");
        selected_material->Diffuse = glm::vec4(color,1.0);
        current_scene->UpdateMaterial();
    }
}

void Editor::GUI::ShowMaterialEditor()
{
    if(init_nodes)
    {
        ImGui::SetNextWindowSize(ImVec2(1280,800));
        ImGui::SetWindowFocus("Material Editor");
        init_nodes = false;
    }
    
    ImGui::Begin("Material Editor", &ed_material, ImGuiWindowFlags_NoDocking);
    
    myNode->rightClickPopUpContent([](ImFlow::BaseNode *node)
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
                    myNode->placeNodeAt<Nodes::NodeSetFloat>(pos, "Test", 1);
                    ImGui::CloseCurrentPopup();
                }
            } });

    myNode->droppedLinkPopUpContent([](ImFlow::Pin *dragged)
                                   { dragged->deleteLink(); });

    std::vector<std::weak_ptr<ImFlow::Link>> myLinks = myNode->getLinks();

    ImGui::Text("Links: %lu", myLinks.size());
    ImGui::SameLine();
    ImGui::Text("Nodes: %u", myNode->getNodesCount());

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

    myNode->update();
    ImGui::End();
}


