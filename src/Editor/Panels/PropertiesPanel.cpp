#include "Editor/EditorUi.hpp"
#include "Editor/Panels/PropertyWidgets.h"

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Command/RenameNodeCommand.hpp"
#include "Runtime/Engine.hpp"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui_stdlib.h>

#include <glm/gtc/quaternion.hpp>

namespace Editor
{
    void DrawPropertiesPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Properties", nullptr);
        {
            if (ui.selected_obj_id == InvalidId)
            {
                ImGui::End();
                return;
            }

            Assets::Node* selectedObj = ctx.scene.GetNodeByInstanceId(ui.selected_obj_id);
            if (selectedObj == nullptr)
            {
                ImGui::End();
                return;
            }

            if (ui.fontIcon)
            {
                ImGui::PushFont(ui.fontIcon);
            }
            ImGui::TextUnformatted(selectedObj->GetName().c_str());
            if (ui.fontIcon)
            {
                ImGui::PopFont();
            }

            ImGui::TextUnformatted("Name");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            static uint32_t editingNodeId = InvalidId;
            static std::string editingName;
            if (editingNodeId != selectedObj->GetInstanceId())
            {
                editingNodeId = selectedObj->GetInstanceId();
                editingName = selectedObj->GetName();
            }

            const bool nameSubmitted =
                ImGui::InputText("##NodeRenameInput", &editingName, ImGuiInputTextFlags_EnterReturnsTrue);
            const bool nameEditFinished = nameSubmitted || ImGui::IsItemDeactivatedAfterEdit();
            if (nameEditFinished)
            {
                if (editingName.empty())
                {
                    editingName = selectedObj->GetName();
                }
                else if (editingName != selectedObj->GetName())
                {
                    ctx.engine.ExecuteCommand(std::make_unique<RenameNodeCommand>(
                        ctx.scene, selectedObj->GetInstanceId(), editingName));
                }
            }

            ImGui::Separator();
            ImGui::NewLine();

            ImGui::Text(ICON_FA_LOCATION_ARROW " Transform");
            ImGui::Separator();

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            ImGui::Button("L");
            ImGui::SameLine();
            ImGui::PopStyleColor();
            if (ImGui::DragFloat3("##Location", &selectedObj->Translation().x, 0.1f))
            {
                selectedObj->RecalcTransform(true);
                ctx.scene.MarkDirty();
            }
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f));
            ImGui::Button("R");
            ImGui::SameLine();
            ImGui::PopStyleColor();
            glm::vec3 eular = glm::eulerAngles(selectedObj->Rotation());
            if (ImGui::DragFloat3("##Rotation", &eular.x, 0.1f))
            {
                selectedObj->SetRotation(glm::quat(eular));
                selectedObj->RecalcTransform(true);
                ctx.scene.MarkDirty();
            }
            ImGui::EndGroup();

            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.8f, 1.0f));
            ImGui::Button("S");
            ImGui::SameLine();
            ImGui::PopStyleColor();
            if (ImGui::DragFloat3("##Scale", &selectedObj->Scale().x, 0.1f))
            {
                selectedObj->RecalcTransform(true);
                ctx.scene.MarkDirty();
            }
            ImGui::EndGroup();

            ImGui::NewLine();
            ImGui::Text(ICON_FA_CUBE " Mesh");
            ImGui::Separator();
            auto render = selectedObj->GetComponent<Runtime::RenderComponent>();
            int modelId = render ? render->GetModelId() : -1;
            ImGui::InputInt("##ModelId", &modelId, 1, 1, ImGuiInputTextFlags_ReadOnly);

            ImGui::NewLine();
            ImGui::Text(ICON_FA_CIRCLE_HALF_STROKE " Material");
            ImGui::Separator();

            if (modelId != -1 && render)
            {
                auto& mats = render->Materials();
                for (auto& mat : mats)
                {
                    const int matIdx = mat;
                    if (matIdx == 0)
                    {
                        continue;
                    }

                    auto& refMat = ctx.scene.Materials()[matIdx];

                    ImGui::PushID(matIdx);
                    ImGui::InputText("##MatName", &refMat.name_, ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopID();

                    ImGui::SameLine();

                    if (ImGui::Button(ICON_FA_CIRCLE_LEFT))
                    {
                        if (ui.selectedMaterialId != InvalidId)
                        {
                            mat = static_cast<int>(ui.selectedMaterialId);
                        }
                    }

                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_PEN_TO_SQUARE))
                    {
                        ui.selected_material = &(ctx.scene.Materials()[matIdx]);
                        ui.ed_material = true;
                        OpenMaterialEditor(ctx, ui);
                    }
                }
            }
            
            // Draw component properties using reflection
            ImGui::NewLine();
            ImGui::Text(ICON_FA_PUZZLE_PIECE " Components");
            ImGui::Separator();
            
            const auto& components = selectedObj->GetComponents();
            for (const auto& component : components)
            {
                if (!component) continue;
                
                std::string headerName = std::string(component->GetTypeName());
                if (ImGui::CollapsingHeader(headerName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent();
                    PropertyWidgets::DrawComponentProperties(component.get(), &ctx.engine.GetCommandHistory());
                    ImGui::Unindent();
                }
            }
        }

        ImGui::End();
    }
} // namespace Editor
