#include "EditorUi.hpp"

#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Nodes/NodeMaterial.hpp"
#include "Nodes/NodeSetFloat.hpp"
#include "Nodes/NodeSetInt.hpp"
#include "ImNodeFlow.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Editor
{
    namespace
    {
        std::unique_ptr<ImFlow::ImNodeFlow> gNodeFlow;
        std::weak_ptr<Nodes::NodeMaterial> gMatNode;
        bool gInitNodes = true;

        void ApplyMaterial(EditorContext& ctx, EditorUiState& ui)
        {
            if (ui.selected_material == nullptr)
            {
                return;
            }

            if (std::shared_ptr<Nodes::NodeMaterial> mat = gMatNode.lock())
            {
                const glm::vec3& color = mat->getInVal<glm::vec3>("Albedo");
                ui.selected_material->gpuMaterial_.Diffuse = glm::vec4(color, 1.0f);
                ui.selected_material->gpuMaterial_.Fuzziness = mat->getInVal<float>("Roughness");
                ui.selected_material->gpuMaterial_.Metalness = mat->getInVal<float>("Metalness");
                ui.selected_material->gpuMaterial_.RefractionIndex = mat->getInVal<float>("IOR");
                ctx.scene.UpdateAllMaterials();
            }
        }
    } // namespace

    void OpenMaterialEditor(EditorContext& /*ctx*/, EditorUiState& ui)
    {
        gNodeFlow.reset(new ImFlow::ImNodeFlow());
        gInitNodes = true;

        if (ui.selected_material == nullptr)
        {
            return;
        }

        float baseY = 20.0f;
        float seprateY = 100.0f;
        const float seprateX = 600.0f;

        auto nodeIOR = gNodeFlow->placeNodeAt<Nodes::NodeSetFloat>(ImVec2(30, baseY), "IOR",
                                                                   ui.selected_material->gpuMaterial_.RefractionIndex);
        auto nodeShadingMode = gNodeFlow->placeNodeAt<Nodes::NodeSetInt>(
            ImVec2(30, baseY += seprateY), "ShadingMode",
            static_cast<int>(ui.selected_material->gpuMaterial_.MaterialModel));

        auto nodeAlbedo = gNodeFlow->placeNodeAt<Nodes::NodeSetColor>(
            ImVec2(30, baseY += seprateY), "Albedo", glm::vec3(ui.selected_material->gpuMaterial_.Diffuse));
        auto nodeRoughness = gNodeFlow->placeNodeAt<Nodes::NodeSetFloat>(ImVec2(30, baseY += seprateY), "Roughness",
                                                                         ui.selected_material->gpuMaterial_.Fuzziness);
        auto nodeMetalness = gNodeFlow->placeNodeAt<Nodes::NodeSetFloat>(ImVec2(30, baseY += seprateY), "Metalness",
                                                                         ui.selected_material->gpuMaterial_.Metalness);

        auto nodeMat = gNodeFlow->placeNodeAt<Nodes::NodeMaterial>(ImVec2(seprateX, baseY * 0.5f - seprateY));
        gMatNode = nodeMat;

        nodeIOR->outPin("Out")->createLink(nodeMat->inPin("IOR"));
        nodeShadingMode->outPin("Out")->createLink(nodeMat->inPin("ShadingMode"));

        nodeAlbedo->outPin("Out")->createLink(nodeMat->inPin("Albedo"));
        nodeRoughness->outPin("Out")->createLink(nodeMat->inPin("Roughness"));
        nodeMetalness->outPin("Out")->createLink(nodeMat->inPin("Metalness"));

        seprateY = 200.0f;
        if (ui.selected_material->gpuMaterial_.DiffuseTextureId != -1)
        {
            auto nodeTexture = gNodeFlow->placeNodeAt<Nodes::NodeSetTexture>(
                ImVec2(30, baseY += seprateY), "AlbedoTexture", ui.selected_material->gpuMaterial_.DiffuseTextureId);
            nodeTexture->outPin("Out")->createLink(nodeMat->inPin("AlbedoTexture"));
        }

        if (ui.selected_material->gpuMaterial_.NormalTextureId != -1)
        {
            auto nodeTexture = gNodeFlow->placeNodeAt<Nodes::NodeSetTexture>(
                ImVec2(30, baseY += seprateY), "NormalTexture", ui.selected_material->gpuMaterial_.NormalTextureId);
            nodeTexture->outPin("Out")->createLink(nodeMat->inPin("NormalTexture"));
        }

        if (ui.selected_material->gpuMaterial_.MRATextureId != -1)
        {
            auto nodeTexture = gNodeFlow->placeNodeAt<Nodes::NodeSetTexture>(
                ImVec2(30, baseY += seprateY), "MRATexture", ui.selected_material->gpuMaterial_.MRATextureId);
            nodeTexture->outPin("Out")->createLink(nodeMat->inPin("MRATexture"));
        }
    }

    void DrawMaterialEditorPanel(EditorContext& ctx, EditorUiState& ui)
    {
        if (!gNodeFlow)
        {
            OpenMaterialEditor(ctx, ui);
        }

        if (gInitNodes)
        {
            ImGui::SetNextWindowSize(ImVec2(1280, 800));
            ImGui::SetWindowFocus("Material Editor");

            ImGuiWindowClass windowClass1;
            windowClass1.ClassId = ImGui::GetID("Material Editor");
            windowClass1.ViewportFlagsOverrideSet = ImGuiViewportFlags_TopMost | ImGuiViewportFlags_NoAutoMerge;
            ImGui::SetNextWindowClass(&windowClass1);
            gInitNodes = false;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 16));
        ImGui::Begin("Material Editor", &ui.ed_material, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse);
        ImGui::PopStyleVar();

        gNodeFlow->rightClickPopUpContent(
            [](ImFlow::BaseNode* node)
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
                        gNodeFlow->placeNodeAt<Nodes::NodeSetFloat>(pos, "Test", 1.0f);
                        ImGui::CloseCurrentPopup();
                    }
                }
            });

        gNodeFlow->droppedLinkPopUpContent([](ImFlow::Pin* dragged) { dragged->deleteLink(); });

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));
        if (ImGui::Button("Apply Material"))
        {
            ApplyMaterial(ctx, ui);
        }
        ImGui::PopStyleVar();

        const std::vector<std::weak_ptr<ImFlow::Link>> links = gNodeFlow->getLinks();
        for (auto wp : links)
        {
            auto p = wp.lock();
            if (!p)
            {
                continue;
            }
            if (p->isSelected() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            {
                ImFlow::Pin* right = p->right();
                ImFlow::Pin* left = p->left();
                right->deleteLink();
                left->deleteLink();
            }
        }

        gNodeFlow->update();
        ImGui::End();
    }
} // namespace Editor
