#include "Editor/EditorUi.hpp"
#include "Editor/Panels/PropertyWidgets.h"

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Command/RenameNodeCommand.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Editor/ProfessionalUI.hpp"
#include "Runtime/Reflection/PropertyAccessor.h"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui_stdlib.h>

#include <cmath>
#include <fmt/format.h>
#include <functional>
#include <glm/gtc/quaternion.hpp>

namespace Editor
{
    namespace
    {
        bool DrawAxisFloat3(const char* label, glm::vec3& value, float speed)
        {
            bool changed = false;
            constexpr ImVec4 axisColors[] = {
                ImVec4(0.90f, 0.20f, 0.18f, 1.0f),
                ImVec4(0.22f, 0.78f, 0.34f, 1.0f),
                ImVec4(0.24f, 0.48f, 0.95f, 1.0f),
            };
            constexpr const char* axisIds[] = {"X", "Y", "Z"};

            ImGui::PushID(label);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::SameLine(82.0f);

            const float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
            const float width = std::max(58.0f, (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f);
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            for (int axis = 0; axis < 3; ++axis)
            {
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float height = ImGui::GetFrameHeight();
                drawList->AddRectFilled(pos, ImVec2(pos.x + 2.0f, pos.y + height),
                                        ImGui::GetColorU32(axisColors[axis]), 1.0f);
                ImGui::SetCursorScreenPos(ImVec2(pos.x + 5.0f, pos.y));
                ImGui::SetNextItemWidth(width - 5.0f);
                changed = ImGui::DragFloat(axisIds[axis], &value[axis], speed, 0.0f, 0.0f, "%.3f") || changed;
                if (axis < 2)
                {
                    ImGui::SameLine(0.0f, spacing);
                }
            }

            ImGui::PopID();
            return changed;
        }
    } // namespace

    void DrawPropertiesPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Properties", nullptr);
        {
            static ImGuiTextFilter propertyFilter;
            propertyFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Search properties##PropertiesSearch", -FLT_MIN);
            Runtime::UiTheme::DrawThinSeparator();

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

            const std::string inspectorSubtitle = "Instance " + std::to_string(selectedObj->GetInstanceId());
            Runtime::UiTheme::DrawPanelHeader(ICON_FA_SLIDERS, "Inspector", inspectorSubtitle.c_str());

            auto render = selectedObj->GetComponent<Runtime::RenderComponent>();
            auto physics = selectedObj->GetComponent<Runtime::PhysicsComponent>();
            bool enabled = render == nullptr || render->GetVisible();
            if (ImGui::Checkbox("##ObjectEnabled", &enabled) && render != nullptr)
            {
                render->SetVisible(enabled);
                ctx.scene.MarkDirty();
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(selectedObj->GetName().c_str());
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetContentRegionMax().x - 70.0f));
            bool isStatic = physics == nullptr || physics->GetMobility() == Runtime::ENodeMobility::Static;
            if (ImGui::Checkbox("Static", &isStatic) && physics != nullptr)
            {
                physics->SetMobility(isStatic ? Runtime::ENodeMobility::Static : Runtime::ENodeMobility::Dynamic);
                ctx.scene.MarkDirty();
            }

            static int tagIndex = 0;
            static int layerIndex = 0;
            ImGui::SetNextItemWidth((ImGui::GetContentRegionAvail().x - 8.0f) * 0.5f);
            ImGui::Combo("##TagSelector", &tagIndex, "Untagged\0Player\0Environment\0Interactable\0\0");
            Runtime::UiTheme::DrawTooltip("Tag");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::Combo("##LayerSelector", &layerIndex, "Default\0Gameplay\0Props\0Colliders\0Lighting\0\0");
            Runtime::UiTheme::DrawTooltip("Layer");

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

            Runtime::UiTheme::DrawThinSeparator();

            if (Runtime::UiTheme::BeginSection(ICON_FA_LOCATION_ARROW, "Transform", true))
            {
                if (DrawAxisFloat3("Location", selectedObj->Translation(), 0.1f))
                {
                    selectedObj->RecalcTransform(true);
                    ctx.scene.MarkDirty();
                }

                glm::vec3 eular = glm::eulerAngles(selectedObj->Rotation());
                if (DrawAxisFloat3("Rotation", eular, 0.1f))
                {
                    selectedObj->SetRotation(glm::quat(eular));
                    selectedObj->RecalcTransform(true);
                    ctx.scene.MarkDirty();
                }

                if (DrawAxisFloat3("Scale", selectedObj->Scale(), 0.1f))
                {
                    selectedObj->RecalcTransform(true);
                    ctx.scene.MarkDirty();
                }
                Runtime::UiTheme::EndSection();
            }
            int modelId = render ? render->GetModelId() : -1;
            if (Runtime::UiTheme::BeginSection(ICON_FA_CUBE, "Mesh", true))
            {
                if (render != nullptr)
                {
                    const std::string preview = modelId >= 0 && modelId < static_cast<int>(ctx.scene.Models().size())
                        ? ctx.scene.Models()[modelId].Name()
                        : "None";
                    if (ImGui::BeginCombo("Model", preview.c_str()))
                    {
                        if (ImGui::Selectable("None", modelId == -1))
                        {
                            render->SetModelId(static_cast<uint32_t>(-1));
                            ctx.scene.MarkDirty();
                        }
                        for (int i = 0; i < static_cast<int>(ctx.scene.Models().size()); ++i)
                        {
                            const std::string itemName = fmt::format("{}: {}", i, ctx.scene.Models()[i].Name());
                            if (ImGui::Selectable(itemName.c_str(), modelId == i))
                            {
                                render->SetModelId(static_cast<uint32_t>(i));
                                ctx.scene.MarkDirty();
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::TextDisabled("No RenderComponent");
                }
                Runtime::UiTheme::EndSection();
            }

            if (Runtime::UiTheme::BeginSection(ICON_FA_CIRCLE_HALF_STROKE, "Material", true))
            {
                if (modelId != -1 && render)
                {
                    auto mats = render->GetMaterials();
                    bool materialsChanged = false;
                    for (int elementIndex = 0; elementIndex < static_cast<int>(mats.size()); ++elementIndex)
                    {
                        uint32_t& mat = mats[elementIndex];
                        const std::string comboLabel = fmt::format("Element {}", elementIndex);
                        const std::string preview = mat < ctx.scene.Materials().size()
                            ? ctx.scene.Materials()[mat].name_
                            : "None";
                        ImGui::PushID(elementIndex);
                        if (ImGui::BeginCombo(comboLabel.c_str(), preview.c_str()))
                        {
                            for (int materialIndex = 0; materialIndex < static_cast<int>(ctx.scene.Materials().size());
                                 ++materialIndex)
                            {
                                const std::string itemName =
                                    fmt::format("{}: {}", materialIndex, ctx.scene.Materials()[materialIndex].name_);
                                if (ImGui::Selectable(itemName.c_str(), mat == static_cast<uint32_t>(materialIndex)))
                                {
                                    mat = static_cast<uint32_t>(materialIndex);
                                    materialsChanged = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SameLine();
                        if (Runtime::UiTheme::IconButton(ICON_FA_PEN_TO_SQUARE, "Edit Material", false,
                                                         ImVec2(28.0f, 24.0f)) &&
                            mat < ctx.scene.Materials().size())
                        {
                            ui.selected_material = &(ctx.scene.Materials()[mat]);
                            ui.ed_material = true;
                            OpenMaterialEditor(ctx, ui);
                        }
                        ImGui::PopID();
                    }
                    if (materialsChanged)
                    {
                        render->SetMaterials(mats);
                        ctx.scene.MarkDirty();
                    }
                }
                Runtime::UiTheme::EndSection();
            }
            
            if (Runtime::UiTheme::BeginSection(ICON_FA_PUZZLE_PIECE, "Components", true))
            {
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
                if (ImGui::Button(ICON_FA_PLUS " Add Component", ImVec2(-FLT_MIN, 0.0f)))
                {
                    ImGui::OpenPopup("AddComponentPopup");
                }
                if (ImGui::BeginPopup("AddComponentPopup"))
                {
                    ImGui::MenuItem("Render Component", nullptr, false, false);
                    ImGui::MenuItem("Physics Component", nullptr, false, false);
                    ImGui::MenuItem("Skinned Mesh Component", nullptr, false, false);
                    ImGui::MenuItem("Script Component", nullptr, false, false);
                    ImGui::EndPopup();
                }
                Runtime::UiTheme::EndSection();
            }
        }

        ImGui::End();
    }
} // namespace Editor
