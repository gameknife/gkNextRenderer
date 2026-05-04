#include "Editor/EditorUi.hpp"
#include "Editor/Panels/PropertyWidgets.h"

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Command/RenameNodeCommand.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Reflection/PropertyAccessor.h"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui_stdlib.h>

#include <cmath>
#include <functional>
#include <glm/gtc/quaternion.hpp>

namespace Editor
{
    void DrawPropertiesPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Properties", nullptr);
        {
            std::vector<uint32_t> selectedIds = ctx.scene.GetSelectedIds();
            if (selectedIds.empty() && ui.selected_obj_id != InvalidId)
            {
                selectedIds.push_back(ui.selected_obj_id);
            }

            if (selectedIds.empty())
            {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                const char* line1 = ICON_FA_CIRCLE_INFO " No object selected";
                const char* line2 = "Select an object from the Outliner or Viewport";
                const ImVec2 textSize1 = ImGui::CalcTextSize(line1);
                const ImVec2 textSize2 = ImGui::CalcTextSize(line2);
                ImGui::SetCursorPos(ImVec2(
                    (avail.x - textSize1.x) * 0.5f,
                    (avail.y - textSize1.y - textSize2.y) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                ImGui::TextUnformatted(line1);
                ImGui::SetCursorPosX((avail.x - textSize2.x) * 0.5f);
                ImGui::TextUnformatted(line2);
                ImGui::PopStyleColor();
                ImGui::End();
                return;
            }

            if (selectedIds.size() > 1)
            {
                Assets::Node* activeObj = ctx.scene.GetNodeByInstanceId(ui.selected_obj_id);
                if (activeObj == nullptr)
                {
                    for (uint32_t id : selectedIds)
                    {
                        activeObj = ctx.scene.GetNodeByInstanceId(id);
                        if (activeObj != nullptr)
                        {
                            break;
                        }
                    }
                }

                if (activeObj == nullptr)
                {
                    ImGui::TextDisabled("%s %d objects selected, no active node",
                                        ICON_FA_CIRCLE_INFO, static_cast<int>(selectedIds.size()));
                    ImGui::End();
                    return;
                }

                ImGui::Text("%d Objects Selected", static_cast<int>(selectedIds.size()));
                ImGui::TextDisabled("Active: %s", activeObj->GetName().c_str());
                ImGui::Separator();

                auto applyTransformToSelection = [&](const std::function<void(Assets::Node&)>& apply)
                {
                    bool changed = false;
                    for (uint32_t id : selectedIds)
                    {
                        Assets::Node* node = ctx.scene.GetNodeByInstanceId(id);
                        if (node == nullptr)
                        {
                            continue;
                        }

                        apply(*node);
                        changed = true;
                    }

                    if (changed)
                    {
                        ctx.scene.MarkDirty();
                    }
                };

                ImGui::Text(ICON_FA_LOCATION_ARROW " Transform");
                ImGui::Separator();

                glm::vec3 location = activeObj->Translation();
                const glm::vec3 baseLocation = location;
                if (ImGui::DragFloat3("##MultiLocation", &location.x, 0.1f))
                {
                    const glm::vec3 delta = location - baseLocation;
                    applyTransformToSelection(
                        [&](Assets::Node& node)
                        {
                            node.SetTranslation(node.Translation() + delta);
                            node.RecalcTransform(true);
                        });
                }

                glm::vec3 rotationEuler = glm::eulerAngles(activeObj->Rotation());
                const glm::vec3 baseRotationEuler = rotationEuler;
                if (ImGui::DragFloat3("##MultiRotation", &rotationEuler.x, 0.1f))
                {
                    const glm::vec3 delta = rotationEuler - baseRotationEuler;
                    applyTransformToSelection(
                        [&](Assets::Node& node)
                        {
                            glm::vec3 nodeEuler = glm::eulerAngles(node.Rotation()) + delta;
                            node.SetRotation(glm::quat(nodeEuler));
                            node.RecalcTransform(true);
                        });
                }

                glm::vec3 scale = activeObj->Scale();
                const glm::vec3 baseScale = scale;
                if (ImGui::DragFloat3("##MultiScale", &scale.x, 0.1f))
                {
                    const glm::vec3 addDelta = scale - baseScale;
                    glm::vec3 mulFactor(1.0f, 1.0f, 1.0f);
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (std::abs(baseScale[axis]) > 1e-4f)
                        {
                            mulFactor[axis] = scale[axis] / baseScale[axis];
                        }
                    }

                    applyTransformToSelection(
                        [&](Assets::Node& node)
                        {
                            glm::vec3 nodeScale = node.Scale();
                            for (int axis = 0; axis < 3; ++axis)
                            {
                                if (std::abs(baseScale[axis]) > 1e-4f)
                                {
                                    nodeScale[axis] *= mulFactor[axis];
                                }
                                else
                                {
                                    nodeScale[axis] += addDelta[axis];
                                }
                            }
                            node.SetScale(nodeScale);
                            node.RecalcTransform(true);
                        });
                }

                ImGui::Separator();
                ImGui::TextDisabled("Multi-selection mode: Mesh/Material/Components editing is disabled.");
                ImGui::End();
                return;
            }

            Assets::Node* selectedObj = ctx.scene.GetNodeByInstanceId(selectedIds.front());
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
                    ctx.engine.GetCommandHistory().Execute(std::make_unique<RenameNodeCommand>(
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
                auto mats = render->GetMaterials();
                bool materialsChanged = false;
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
                            materialsChanged = true;
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
                if (materialsChanged)
                {
                    render->SetMaterials(mats);
                    ctx.scene.MarkDirty();
                }
            }
            
            // Draw component properties using reflection
            ImGui::NewLine();
            ImGui::Text(ICON_FA_PUZZLE_PIECE " Components");
            ImGui::Separator();

            static ImGuiTextFilter propertyFilter;
            propertyFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Filter##Properties", 220.0f);

            const auto& components = selectedObj->GetComponents();
            for (const auto& component : components)
            {
                if (!component) continue;

                // Skip component if all its properties are filtered out
                if (propertyFilter.IsActive())
                {
                    auto metaType = component->GetMetaType();
                    auto props = Reflection::PropertyAccessor::GetProperties(metaType);
                    bool anyVisible = false;
                    for (const auto& prop : props)
                    {
                        const char* displayName = prop.meta.displayName.empty()
                            ? prop.name.c_str() : prop.meta.displayName.c_str();
                        if (propertyFilter.PassFilter(displayName))
                        {
                            anyVisible = true;
                            break;
                        }
                    }
                    if (!anyVisible) continue;
                }

                std::string headerName = std::string(component->GetTypeName());
                if (ImGui::CollapsingHeader(headerName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ImGui::Indent();
                    if (PropertyWidgets::DrawComponentProperties(component.get(), &ctx.engine.GetCommandHistory(),
                                                                 PropertyWidgets::WidgetConfig(), &propertyFilter))
                    {
                        ctx.scene.MarkDirty();
                    }
                    ImGui::Unindent();
                }
            }
        }

        ImGui::End();
    }
} // namespace Editor
