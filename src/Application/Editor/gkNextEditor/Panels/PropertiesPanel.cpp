#include "EditorUi.hpp"
#include "Panels/PropertyWidgets.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Application/Editor/Common/Preview/AssetThumbnailRenderer.hpp"
#include "Modules/DevTools/Command/RenameNodeCommand.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Engine/Runtime/Reflection/PropertyAccessor.hpp"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui_stdlib.h>

#include <cmath>
#include <functional>
#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>

namespace Editor
{
    namespace
    {
        size_t MaterialSlotCount(const Assets::Model& model, size_t capacity)
        {
            return std::min(static_cast<size_t>(model.MaterialSlotCount()), capacity);
        }

        bool DrawAxisFloat3(const char* label, glm::vec3& value, float speed)
        {
            bool changed = false;
            constexpr ImVec4 axisColors[] = {
                ImVec4(0.90f, 0.20f, 0.18f, 1.0f),
                ImVec4(0.22f, 0.78f, 0.34f, 1.0f),
                ImVec4(0.24f, 0.48f, 0.95f, 1.0f),
            };
            constexpr const char* axisIds[] = {"##X", "##Y", "##Z"};
            constexpr float axisAccentWidth = 3.0f;
            constexpr float axisAccentInset = 3.0f;
            constexpr float axisAccentGap = 4.0f;

            ImGui::PushID(label);
            NextUI::Theme::BeginFormRow(label, 0.22f, 70.0f, 70.0f);

            const ImGuiStyle& style = ImGui::GetStyle();
            const float spacing = style.ItemSpacing.x;
            const float width = std::max(54.0f, (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                                ImVec2(style.FramePadding.x + axisAccentWidth + axisAccentGap, style.FramePadding.y));

            for (int axis = 0; axis < 3; ++axis)
            {
                ImGui::SetNextItemWidth(width);
                changed = ImGui::DragFloat(axisIds[axis], &value[axis], speed, 0.0f, 0.0f, "%.3f") || changed;
                const ImVec2 itemMin = ImGui::GetItemRectMin();
                const ImVec2 itemMax = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    itemMin + ImVec2(axisAccentInset, axisAccentInset),
                    ImVec2(itemMin.x + axisAccentInset + axisAccentWidth, itemMax.y - axisAccentInset),
                    ImGui::GetColorU32(axisColors[axis]), 1.5f);
                if (axis < 2)
                {
                    ImGui::SameLine(0.0f, spacing);
                }
            }

            ImGui::PopStyleVar();
            ImGui::PopID();
            return changed;
        }
    } // namespace

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

            auto render = selectedObj->GetComponent<Runtime::RenderComponent>();
            auto physics = selectedObj->GetComponent<Runtime::PhysicsComponent>();
            if (ui.propertiesState.editingNodeId != selectedObj->GetInstanceId())
            {
                ui.propertiesState.editingNodeId = selectedObj->GetInstanceId();
                ui.propertiesState.editingName = selectedObj->GetName();
                ui.propertiesState.renamingName = false;
                ui.propertiesState.focusNameInput = false;
            }

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));

            ImGui::PushFont(NextUI::Theme::GetTitleFont(ctx.engine));
            bool enabled = render == nullptr || render->GetVisible();
            const char* visibilityIcon = enabled ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
            if (render == nullptr) ImGui::BeginDisabled();
            if (NextUI::Theme::IconButton(visibilityIcon, enabled ? "Visible" : "Hidden", false,
                                          ImVec2(0.0f, ImGui::GetFrameHeight())) && render != nullptr)
            {
                enabled = !enabled;
                render->SetVisible(enabled);
                ctx.scene.MarkDirty();
            }
            if (render == nullptr) ImGui::EndDisabled();

            ImGui::SameLine();
            if (ui.propertiesState.renamingName)
            {
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ui.propertiesState.focusNameInput)
                {
                    ImGui::SetKeyboardFocusHere();
                    ui.propertiesState.focusNameInput = false;
                }

                const bool nameSubmitted =
                    ImGui::InputText("##NodeRenameInput", &ui.propertiesState.editingName,
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                const bool cancelRename = ImGui::IsKeyPressed(ImGuiKey_Escape);
                const bool nameEditFinished = nameSubmitted || ImGui::IsItemDeactivated();
                if (cancelRename)
                {
                    ui.propertiesState.editingName = selectedObj->GetName();
                    ui.propertiesState.renamingName = false;
                }
                else if (nameEditFinished)
                {
                    if (ui.propertiesState.editingName.empty())
                    {
                        ui.propertiesState.editingName = selectedObj->GetName();
                    }
                    else if (ui.propertiesState.editingName != selectedObj->GetName())
                    {
                        ctx.engine.GetCommandHistory().Execute(std::make_unique<Runtime::Command::RenameNodeCommand>(
                            ctx.scene, selectedObj->GetInstanceId(), ui.propertiesState.editingName));
                    }
                    ui.propertiesState.renamingName = false;
                }
            }
            else
            {
                const std::string title = fmt::format("{}###ObjectTitle", selectedObj->GetName());
                if (ImGui::Selectable(title.c_str(), false, ImGuiSelectableFlags_None,
                                      ImVec2(0.0f, ImGui::GetFrameHeight())))
                {
                    ui.propertiesState.editingName = selectedObj->GetName();
                    ui.propertiesState.renamingName = true;
                    ui.propertiesState.focusNameInput = true;
                }
                NextUI::Theme::DrawTooltip("Click to rename");
            }
            ImGui::PopFont();

            bool isStatic = physics == nullptr || physics->GetMobility() == Runtime::ENodeMobility::Static;

            static constexpr const char* tagItems[] = {"Untagged", "Player", "Environment", "Interactable"};
            static constexpr const char* layerItems[] = {"Default", "Gameplay", "Props", "Colliders", "Lighting"};
            auto findItemIndex = [](const char* const* items, int count, const std::string& value)
            {
                for (int i = 0; i < count; ++i)
                {
                    if (value == items[i])
                    {
                        return i;
                    }
                }
                return 0;
            };
            int tagIndex = findItemIndex(tagItems, IM_ARRAYSIZE(tagItems), selectedObj->GetTag());
            int layerIndex = findItemIndex(layerItems, IM_ARRAYSIZE(layerItems), selectedObj->GetLayer());

            constexpr ImGuiTableFlags summaryFlags =
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoSavedSettings;
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(2.0f, 2.0f));
            if (ImGui::BeginTable("##ObjectClassification", 3, summaryFlags))
            {
                ImGui::TableSetupColumn("Mobility", ImGuiTableColumnFlags_WidthStretch, 0.75f);
                ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthStretch, 1.15f);
                ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch, 0.90f);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (physics == nullptr) ImGui::BeginDisabled();
                if (ImGui::BeginCombo("##MobilitySelector", isStatic ? "Static" : "Dynamic"))
                {
                    if (ImGui::Selectable(ICON_FA_LOCK " Static", isStatic))
                    {
                        physics->SetMobility(Runtime::ENodeMobility::Static);
                        ctx.scene.MarkDirty();
                    }
                    if (ImGui::Selectable(ICON_FA_PERSON_RUNNING " Dynamic", !isStatic))
                    {
                        physics->SetMobility(Runtime::ENodeMobility::Dynamic);
                        ctx.scene.MarkDirty();
                    }
                    ImGui::EndCombo();
                }
                NextUI::Theme::DrawTooltip("Mobility");
                if (physics == nullptr) ImGui::EndDisabled();

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##TagSelector", tagItems[tagIndex]))
                {
                    for (int i = 0; i < IM_ARRAYSIZE(tagItems); ++i)
                    {
                        if (ImGui::Selectable(tagItems[i], tagIndex == i))
                        {
                            selectedObj->SetTag(tagItems[i]);
                            ctx.scene.MarkDirty();
                        }
                    }
                    ImGui::EndCombo();
                }
                NextUI::Theme::DrawTooltip("Tag");

                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##LayerSelector", layerItems[layerIndex]))
                {
                    for (int i = 0; i < IM_ARRAYSIZE(layerItems); ++i)
                    {
                        if (ImGui::Selectable(layerItems[i], layerIndex == i))
                        {
                            selectedObj->SetLayer(layerItems[i]);
                            ctx.scene.MarkDirty();
                        }
                    }
                    ImGui::EndCombo();
                }
                NextUI::Theme::DrawTooltip("Layer");

                ImGui::EndTable();
            }
            ImGui::PopStyleVar();

            ImGui::PopStyleVar();

            NextUI::Theme::DrawThinSeparator();

            if (NextUI::Theme::BeginSection(ICON_FA_LOCATION_ARROW, "Transform", true))
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
                NextUI::Theme::EndSection();
            }
            int modelId = render ? render->GetModelId() : -1;

            if (NextUI::Theme::BeginSection(ICON_FA_PUZZLE_PIECE, "Components", true))
            {
                ui.propertiesState.propertyFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Search##PropertiesSearch");
                NextUI::Theme::DrawThinSeparator();
                const auto& components = selectedObj->GetComponents();
                for (const auto& component : components)
                {
                    if (!component)
                        continue;

                    // Skip component if all its properties are filtered out
                    if (ui.propertiesState.propertyFilter.IsActive())
                    {
                        auto metaType = component->GetMetaType();
                        auto props = Reflection::PropertyAccessor::GetProperties(metaType);
                        bool anyVisible = false;
                        for (const auto& prop : props)
                        {
                            const char* displayName = prop.meta.displayName.empty()
                                ? prop.name.c_str()
                                : prop.meta.displayName.c_str();
                            if (ui.propertiesState.propertyFilter.PassFilter(displayName))
                            {
                                anyVisible = true;
                                break;
                            }
                        }
                        if (!anyVisible)
                            continue;
                    }

                    std::string headerName = std::string(component->GetTypeName());
                    if (ImGui::CollapsingHeader(headerName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        //ImGui::Indent();
                        PropertyWidgets::WidgetConfig widgetConfig;
                        if (component.get() == render)
                        {
                            if (modelId >= 0 && modelId < static_cast<int>(ctx.scene.Models().size()))
                            {
                                widgetConfig.arrayDisplayLimit = MaterialSlotCount(
                                    ctx.scene.Models()[modelId], render->GetMaterials().size());
                            }
                            widgetConfig.modelAsset.thumbnail = [&ctx](const uint32_t id) -> ImTextureID
                            {
                                if (id >= ctx.scene.Models().size()) return 0;
                                const uint32_t sampleSlot = EditorPreview::AssetThumbnails(ctx.engine.GetRenderer())
                                    .RequestMeshThumbnail(id, ctx.scene.Models()[id]);
                                return sampleSlot == std::numeric_limits<uint32_t>::max()
                                    ? 0
                                    : ctx.ui.RequestImTextureIdRaw(sampleSlot);
                            };
                            widgetConfig.modelAsset.name = [&ctx](const uint32_t id)
                            {
                                return id < ctx.scene.Models().size()
                                    ? ctx.scene.Models()[id].Name()
                                    : fmt::format("Missing model #{}", id);
                            };
                            widgetConfig.modelAsset.selectedAsset = [&ui, &ctx]()
                            {
                                return ui.selectedMeshId < ctx.scene.Models().size()
                                    ? ui.selectedMeshId
                                    : InvalidId;
                            };
                            widgetConfig.modelAsset.locateAsset = [&ui, &ctx](const uint32_t id)
                            {
                                if (id >= ctx.scene.Models().size()) return;
                                ui.contentBrowser = true;
                                ui.selectedMeshId = id;
                                ui.contentBrowserState.currentSection = 3;
                                ui.contentBrowserState.meshFilter.Clear();
                                ui.contentBrowserState.pendingRevealMeshId = id;
                            };
                            widgetConfig.materialAsset.thumbnail = [&ctx](const uint32_t id) -> ImTextureID
                            {
                                if (id >= ctx.scene.Materials().size()) return 0;
                                const uint32_t sampleSlot = EditorPreview::AssetThumbnails(ctx.engine.GetRenderer())
                                    .RequestMaterialThumbnail(id, ctx.scene.Materials()[id]);
                                return sampleSlot == std::numeric_limits<uint32_t>::max()
                                    ? 0
                                    : ctx.ui.RequestImTextureIdRaw(sampleSlot);
                            };
                            widgetConfig.materialAsset.name = [&ctx](const uint32_t id)
                            {
                                return id < ctx.scene.Materials().size()
                                    ? ctx.scene.Materials()[id].name_
                                    : fmt::format("Missing material #{}", id);
                            };
                            widgetConfig.materialAsset.selectedAsset = [&ui, &ctx]()
                            {
                                return ui.selectedMaterialId < ctx.scene.Materials().size()
                                    ? ui.selectedMaterialId
                                    : InvalidId;
                            };
                            widgetConfig.materialAsset.locateAsset = [&ui, &ctx](const uint32_t id)
                            {
                                if (id >= ctx.scene.Materials().size()) return;
                                ui.contentBrowser = true;
                                ui.selectedMaterialId = id;
                                ui.contentBrowserState.currentSection = 1;
                                ui.contentBrowserState.materialFilter.Clear();
                                ui.contentBrowserState.pendingRevealMaterialId = id;
                            };
                            widgetConfig.materialAsset.editAsset = [&ctx, &ui](const uint32_t id)
                            {
                                if (id >= ctx.scene.Materials().size()) return;
                                ui.selectedMaterialId = id;
                                ui.selected_material = &ctx.scene.Materials()[id];
                                ui.ed_material = true;
                                OpenMaterialEditor(ctx, ui);
                            };
                        }
                        if (PropertyWidgets::DrawComponentProperties(component.get(), &ctx.engine.GetCommandHistory(),
                                                                     widgetConfig,
                                                                     &ui.propertiesState.propertyFilter))
                        {
                            ctx.scene.MarkDirty();
                        }
                        //ImGui::Unindent();
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
                NextUI::Theme::EndSection();
            }
        }

        ImGui::End();
    }
} // namespace Editor
