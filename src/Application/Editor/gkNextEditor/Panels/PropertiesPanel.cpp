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
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Engine/Runtime/Reflection/PropertyAccessor.hpp"
#include "EditorActionDispatcher.hpp"
#include "Core/NodeClassification.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"

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

        // Scale is clamped away from zero (a zero-scaled node collapses and cannot be dragged back)
        // but not capped at the top: scenes legitimately contain very large scales.
        constexpr float kMinScale = 0.001f;
        constexpr float kMaxScale = 1.0e6f;

        bool Draw3AxisFloatDrag(const char* label, glm::vec3& value, float speed = 0.1f,
                                float min = 0.0f, float max = 0.0f, float resetValue = 0.0f)
        {
            ImGui::PushID(label);
            const float availWidth = ImGui::GetContentRegionAvail().x;
            constexpr float labelWidth = 70.0f;
            constexpr float resetBtnWidth = 22.0f;
            const float frameHeight = ImGui::GetFrameHeight();
            ImFont* font = ImGui::GetFont();
            const float textSize = ImGui::GetFontSize();
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            // 1. Left Property Label
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "%s", label);
            ImGui::SameLine(labelWidth);

            // 2. Compute widths for 3 axes
            constexpr float axisGap = 6.0f;
            const float inputAreaWidth = std::max(120.0f, availWidth - labelWidth - resetBtnWidth - 6.0f);
            const float axisWidth = std::floor((inputAreaWidth - axisGap * 2.0f) / 3.0f);
            constexpr float tagWidth = 19.0f;
            const float dragWidth = std::max(26.0f, axisWidth - tagWidth);

            bool changed = false;
            struct FAxisDef
            {
                const char* name;
                const char* tagId;
                const char* inputId;
                ImU32 tagCol;
                ImU32 tagHoverCol;
                ImU32 tagActiveCol;
            };
            static constexpr FAxisDef axes[3] = {
                {"X", "##tag_X", "##axis_X",
                 IM_COL32(208, 52, 58, 235), IM_COL32(232, 68, 74, 255), IM_COL32(180, 38, 44, 255)},
                {"Y", "##tag_Y", "##axis_Y",
                 IM_COL32(46, 160, 67, 235), IM_COL32(56, 185, 78, 255), IM_COL32(35, 135, 52, 255)},
                {"Z", "##tag_Z", "##axis_Z",
                 IM_COL32(38, 115, 222, 235), IM_COL32(52, 138, 245, 255), IM_COL32(28, 92, 185, 255)},
            };

            for (int i = 0; i < 3; ++i)
            {
                if (i > 0)
                {
                    ImGui::SameLine(0.0f, axisGap);
                }

                const ImVec2 tagMin = ImGui::GetCursorScreenPos();
                const ImVec2 tagMax(tagMin.x + tagWidth, tagMin.y + frameHeight);
                const ImVec2 dragMin(tagMax.x, tagMin.y);
                const ImVec2 dragMax(dragMin.x + dragWidth, tagMin.y + frameHeight);
                const ImVec2 axisTotalMin = tagMin;
                const ImVec2 axisTotalMax = dragMax;

                // Part A: Left Tag Badge (Invisible button for interactions)
                ImGui::InvisibleButton(axes[i].tagId, ImVec2(tagWidth, frameHeight));
                const bool tagHovered = ImGui::IsItemHovered();
                const bool tagActive = ImGui::IsItemActive();

                if (tagHovered)
                {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                    ImGui::SetTooltip("%s Axis (Drag to scrub, double-click to reset)", axes[i].name);
                }

                // Dragging on Tag directly adjusts value
                if (tagActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
                {
                    value[i] += ImGui::GetIO().MouseDelta.x * speed;
                    if (min < max)
                    {
                        value[i] = std::clamp(value[i], min, max);
                    }
                    changed = true;
                }
                // Double-click Tag to reset axis
                if (tagHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    value[i] = resetValue;
                    changed = true;
                }

                // Render Tag badge with seamless rounded left corners
                const ImU32 tagColor = tagActive ? axes[i].tagActiveCol : (tagHovered ? axes[i].tagHoverCol : axes[i].tagCol);
                drawList->AddRectFilled(tagMin, tagMax, tagColor, 4.0f, ImDrawFlags_RoundCornersLeft);

                const ImVec2 charSize = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, axes[i].name);
                const ImVec2 charPos(tagMin.x + (tagWidth - charSize.x) * 0.5f, tagMin.y + (frameHeight - charSize.y) * 0.5f);
                drawList->AddText(font, textSize, charPos, IM_COL32(255, 255, 255, 245), axes[i].name);

                // Part B: Right Number Drag Box (0 gap, seamless joint with Tag)
                ImGui::SameLine(0.0f, 0.0f);

                const bool isDragAreaHovered = ImGui::IsMouseHoveringRect(dragMin, dragMax);
                const ImU32 dragBgCol = NextUI::Theme::ColorU32(
                    isDragAreaHovered ? NextUI::Theme::EColor::SurfaceHover : NextUI::Theme::EColor::SurfaceElevated,
                    isDragAreaHovered ? 0.90f : 0.65f);
                drawList->AddRectFilled(dragMin, dragMax, dragBgCol, 4.0f, ImDrawFlags_RoundCornersRight);

                // The background is drawn above, so the widget itself stays fully transparent.
                ImGui::SetNextItemWidth(dragWidth);
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));

                if (ImGui::DragFloat(axes[i].inputId, &value[i], speed, min, max, "%.3f"))
                {
                    changed = true;
                }
                const bool dragActive = ImGui::IsItemActive();
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar();

                // If active or focused, highlight the entire composite unit
                if (dragActive)
                {
                    drawList->AddRect(axisTotalMin, axisTotalMax,
                                      NextUI::Theme::ColorU32(NextUI::Theme::EColor::Accent, 0.70f),
                                      4.0f, 0, 1.0f);
                }
            }

            // 3. Right Reset Button
            ImGui::SameLine(0.0f, 6.0f);
            const bool isModified = (std::abs(value.x - resetValue) > 0.0001f ||
                                     std::abs(value.y - resetValue) > 0.0001f ||
                                     std::abs(value.z - resetValue) > 0.0001f);

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.75f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(
                isModified ? NextUI::Theme::EColor::Accent : NextUI::Theme::EColor::TextDim,
                isModified ? 0.95f : 0.40f));

            if (ImGui::Button(ICON_FA_ROTATE_LEFT, ImVec2(resetBtnWidth, frameHeight)))
            {
                value = glm::vec3(resetValue);
                changed = true;
            }
            if (ImGui::IsItemHovered())
            {
                const std::string tooltip = isModified
                    ? fmt::format("Reset {} to default ({:.1f})", label, resetValue)
                    : fmt::format("{} is at default value ({:.1f})", label, resetValue);
                ImGui::SetTooltip("%s", tooltip.c_str());
            }
            ImGui::PopStyleColor(4);
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

            // ========================================================
            // 1. Empty State
            // ========================================================
            if (selectedIds.empty())
            {
                const ImVec2 avail = ImGui::GetContentRegionAvail();
                ImGui::Dummy(ImVec2(0.0f, std::max(20.0f, avail.y * 0.25f)));

                NextUI::Theme::BeginCard("##EmptyPropsCard");
                ImGui::Dummy(ImVec2(0.0f, 16.0f));

                const char* iconStr = ICON_FA_ARROW_POINTER;
                const char* line1 = "No Object Selected";
                const char* line2 = "Select an object from the Outliner or click inside the Viewport to view properties.";

                const ImVec2 textSize1 = ImGui::CalcTextSize(line1);
                const ImVec2 textSize2 = ImGui::CalcTextSize(line2);
                const float cardW = ImGui::GetContentRegionAvail().x;

                ImGui::SetCursorPosX((cardW - ImGui::CalcTextSize(iconStr).x) * 0.5f);
                ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::Accent, 0.70f), "%s", iconStr);
                ImGui::Dummy(ImVec2(0.0f, 6.0f));

                ImGui::SetCursorPosX((cardW - textSize1.x) * 0.5f);
                ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::Text), "%s", line1);
                ImGui::Dummy(ImVec2(0.0f, 4.0f));

                ImGui::PushTextWrapPos(cardW - 10.0f);
                ImGui::SetCursorPosX(16.0f);
                ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextDim), "%s", line2);
                ImGui::PopTextWrapPos();

                ImGui::Dummy(ImVec2(0.0f, 16.0f));
                NextUI::Theme::EndCard();

                ImGui::End();
                return;
            }

            // ========================================================
            // 2. Multi-Selection Mode
            // ========================================================
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

                NextUI::Theme::BeginCard("##MultiSelectionCard");
                ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::Accent), ICON_FA_OBJECT_GROUP "  %d Objects Selected", static_cast<int>(selectedIds.size()));
                ImGui::TextDisabled("Active reference: %s", activeObj->GetName().c_str());
                NextUI::Theme::EndCard();

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

                if (NextUI::Theme::BeginSection(ICON_FA_LOCATION_ARROW, "Transform (Relative)", true))
                {
                    glm::vec3 location = activeObj->Translation();
                    const glm::vec3 baseLocation = location;
                    if (Draw3AxisFloatDrag("Location", location))
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
                    if (Draw3AxisFloatDrag("Rotation", rotationEuler))
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
                    if (Draw3AxisFloatDrag("Scale", scale, 0.1f, kMinScale, kMaxScale, 1.0f))
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
                    NextUI::Theme::EndSection();
                }

                ImGui::Dummy(ImVec2(0.0f, 6.0f));
                ImGui::TextDisabled("Multi-selection mode: component inspection is disabled.");
                ImGui::End();
                return;
            }

            // ========================================================
            // 3. Single Selection Mode
            // ========================================================
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

            // --- Entity Header Card ---
            NextUI::Theme::BeginCard("##EntityHeaderCard");
            {
                // Top Row: Type chip on the left, quick actions on the right. The chip comes from the
                // same classifier the Outliner icon uses, so the two panels always agree.
                const FNodeVisual& visual = ClassifyNode(*selectedObj);
                NextUI::Theme::TagChip(fmt::format("{} {}", visual.icon, visual.label).c_str(), visual.tint,
                                       visual.description);

                // Quick Action Buttons on top-right: Visibility, Focus, Delete
                constexpr float actBtnW = 26.0f;
                constexpr float actBtnH = 22.0f;
                constexpr float actSpacing = 4.0f;
                const float topActionsWidth = actBtnW * 3.0f + actSpacing * 2.0f;
                NextUI::Theme::SameLineRightAligned(topActionsWidth);

                bool enabled = render == nullptr || render->GetVisible();
                const char* visibilityIcon = enabled ? ICON_FA_EYE : ICON_FA_EYE_SLASH;
                if (render == nullptr) ImGui::BeginDisabled();
                if (NextUI::Theme::IconButton(visibilityIcon, enabled ? "Hide Object" : "Show Object", false, ImVec2(actBtnW, actBtnH)) && render != nullptr)
                {
                    render->SetVisible(!enabled);
                    ctx.scene.MarkDirty();
                }
                if (render == nullptr) ImGui::EndDisabled();

                ImGui::SameLine(0.0f, actSpacing);
                if (NextUI::Theme::IconButton(ICON_FA_LOCATION_CROSSHAIRS "##Focus", "Focus in Viewport", false, ImVec2(actBtnW, actBtnH)))
                {
                    ctx.actions.Dispatch(ctx, EEditorAction::Camera_FocusSelected, std::to_string(selectedObj->GetInstanceId()));
                }

                ImGui::SameLine(0.0f, actSpacing);
                if (NextUI::Theme::IconButton(ICON_FA_TRASH_CAN "##Delete", "Delete Object", false, ImVec2(actBtnW, actBtnH)))
                {
                    ctx.engine.GetCommandHistory().Execute(
                        std::make_unique<Runtime::Command::DeleteNodesCommand>(ctx.scene, std::vector<uint32_t>{selectedObj->GetInstanceId()}));
                }

                ImGui::Dummy(ImVec2(0.0f, 2.0f));

                // Middle Row: Object Title with click-to-rename
                ImGui::PushFont(NextUI::Theme::GetTitleFont(ctx.engine));
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

                ImGui::Dummy(ImVec2(0.0f, 4.0f));

                // Bottom Row: Mobility, Tag, Layer
                bool isStatic = physics == nullptr || physics->GetMobility() == Runtime::ENodeMobility::Static;
                static constexpr const char* tagItems[] = {"Untagged", "Player", "Environment", "Interactable"};
                static constexpr const char* layerItems[] = {"Default", "Gameplay", "Props", "Colliders", "Lighting"};
                auto findItemIndex = [](const char* const* items, int count, const std::string& value)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        if (value == items[i]) return i;
                    }
                    return 0;
                };
                int tagIndex = findItemIndex(tagItems, IM_ARRAYSIZE(tagItems), selectedObj->GetTag());
                int layerIndex = findItemIndex(layerItems, IM_ARRAYSIZE(layerItems), selectedObj->GetLayer());

                constexpr ImGuiTableFlags summaryFlags =
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_NoSavedSettings;
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(3.0f, 2.0f));
                if (ImGui::BeginTable("##ObjectClassification", 3, summaryFlags))
                {
                    ImGui::TableSetupColumn("Mobility", ImGuiTableColumnFlags_WidthStretch, 0.85f);
                    ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthStretch, 1.10f);
                    ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch, 0.95f);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (physics == nullptr) ImGui::BeginDisabled();
                    if (ImGui::BeginCombo("##MobilitySelector", isStatic ? ICON_FA_LOCK " Static" : ICON_FA_PERSON_RUNNING " Dynamic"))
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
            }
            NextUI::Theme::EndCard();

            // --- Transform Section ---
            if (NextUI::Theme::BeginSection(ICON_FA_LOCATION_ARROW, "Transform", true))
            {
                if (Draw3AxisFloatDrag("Location", selectedObj->Translation()))
                {
                    selectedObj->RecalcTransform(true);
                    ctx.scene.MarkDirty();
                }

                glm::vec3 euler = glm::eulerAngles(selectedObj->Rotation());
                if (Draw3AxisFloatDrag("Rotation", euler))
                {
                    selectedObj->SetRotation(glm::quat(euler));
                    selectedObj->RecalcTransform(true);
                    ctx.scene.MarkDirty();
                }

                if (Draw3AxisFloatDrag("Scale", selectedObj->Scale(), 0.1f, kMinScale, kMaxScale, 1.0f))
                {
                    selectedObj->RecalcTransform(true);
                    ctx.scene.MarkDirty();
                }
                NextUI::Theme::EndSection();
            }

            // --- Components Section ---
            const int modelId = render ? render->GetModelId() : -1;
            if (NextUI::Theme::BeginSection(ICON_FA_PUZZLE_PIECE, "Components", true))
            {
                ImGui::SetNextItemWidth(-FLT_MIN);
                ui.propertiesState.propertyFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Search##PropertiesSearch");
                NextUI::Theme::DrawThinSeparator(0.50f);

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

                    std::string headerName = fmt::format(ICON_FA_CUBES "  {}", component->GetTypeName());
                    if (ImGui::CollapsingHeader(headerName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        PropertyWidgets::WidgetConfig widgetConfig;
                        widgetConfig.modelAsset.titleFont = ctx.ui.GetTitleBarFont();
                        widgetConfig.materialAsset.titleFont = ctx.ui.GetTitleBarFont();
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
                    }
                }

                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                if (ImGui::Button(ICON_FA_PLUS "  Add Component", ImVec2(-FLT_MIN, 28.0f)))
                {
                    ImGui::OpenPopup("AddComponentPopup");
                }
                ImGui::PopStyleVar();

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
