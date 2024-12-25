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
        float seprateX = 600.0f;
        // from material to node
        auto nodeIOR = myNode->placeNodeAt<Nodes::NodeSetFloat>(ImVec2(30,baseY), "IOR", selected_material->gpuMaterial_.RefractionIndex);
        auto nodeShadingMode = myNode->placeNodeAt<Nodes::NodeSetInt>(ImVec2(30,baseY += seprateY), "ShadingMode", (int)selected_material->gpuMaterial_.MaterialModel);
        
        auto nodeAlbedo = myNode->placeNodeAt<Nodes::NodeSetColor>(ImVec2(30, baseY += seprateY), "Albedo", glm::vec3(selected_material->gpuMaterial_.Diffuse));
        auto nodeRoughness  = myNode->placeNodeAt<Nodes::NodeSetFloat>(ImVec2(30, baseY += seprateY), "Roughness", selected_material->gpuMaterial_.Fuzziness);
        auto nodeMetalness  = myNode->placeNodeAt<Nodes::NodeSetFloat>(ImVec2(30, baseY += seprateY), "Metalness", selected_material->gpuMaterial_.Metalness);


        
        auto nodeMat  = myNode->placeNodeAt<Nodes::NodeMaterial>(ImVec2(seprateX, baseY * 0.5f - seprateY));
        matNode = nodeMat;

        nodeIOR->outPin("Out")->createLink(nodeMat->inPin("IOR"));
        nodeShadingMode->outPin("Out")->createLink(nodeMat->inPin("ShadingMode"));
        
        nodeAlbedo->outPin("Out")->createLink(nodeMat->inPin("Albedo"));
        nodeRoughness->outPin("Out")->createLink(nodeMat->inPin("Roughness"));
        nodeMetalness->outPin("Out")->createLink(nodeMat->inPin("Metalness"));

        seprateY = 200.0f;
        if(selected_material->gpuMaterial_.DiffuseTextureId != -1)
        {
            auto nodeTexture = myNode->placeNodeAt<Nodes::NodeSetTexture>(ImVec2(30,baseY += seprateY), "AlbedoTexture", selected_material->gpuMaterial_.DiffuseTextureId);
            nodeTexture->outPin("Out")->createLink(nodeMat->inPin("AlbedoTexture"));
        }

        if(selected_material->gpuMaterial_.NormalTextureId != -1)
        {
            auto nodeTexture = myNode->placeNodeAt<Nodes::NodeSetTexture>(ImVec2(30,baseY += seprateY), "NormalTexture", selected_material->gpuMaterial_.NormalTextureId);
            nodeTexture->outPin("Out")->createLink(nodeMat->inPin("NormalTexture"));
        }

        if(selected_material->gpuMaterial_.MRATextureId != -1)
        {
            auto nodeTexture = myNode->placeNodeAt<Nodes::NodeSetTexture>(ImVec2(30,baseY += seprateY), "MRATexture", selected_material->gpuMaterial_.MRATextureId);
            nodeTexture->outPin("Out")->createLink(nodeMat->inPin("MRATexture"));
        }
    }
}

void Editor::GUI::ApplyMaterial()
{
    if(std::shared_ptr<Nodes::NodeMaterial> mat = matNode.lock())
    {
        const glm::vec3& color = mat->getInVal<glm::vec3>("Albedo");
        
        selected_material->gpuMaterial_.Diffuse = glm::vec4(color,1.0);
        selected_material->gpuMaterial_.Fuzziness = mat->getInVal<float>("Roughness");
        selected_material->gpuMaterial_.Metalness = mat->getInVal<float>("Metalness");
        selected_material->gpuMaterial_.RefractionIndex = mat->getInVal<float>("IOR");
        
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
                    myNode->placeNodeAt<Nodes::NodeSetFloat>(pos, "Test", 1.0f);
                    ImGui::CloseCurrentPopup();
                }
            } });

    myNode->droppedLinkPopUpContent([](ImFlow::Pin *dragged)
                                   { dragged->deleteLink(); });

    std::vector<std::weak_ptr<ImFlow::Link>> myLinks = myNode->getLinks();

    ImGui::Text("Links: %lu", myLinks.size());
    ImGui::SameLine();
    ImGui::Text("Nodes: %u", myNode->getNodesCount());
    ImGui::SameLine();
    if ( ImGui::Button("Apply Material") )
    {
        ApplyMaterial();
    }

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


