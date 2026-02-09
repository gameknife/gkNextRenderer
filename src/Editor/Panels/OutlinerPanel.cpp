#include "Editor/EditorUi.hpp"

#include "Editor/EditorActionDispatcher.hpp"
#include "Editor/EditorUtils.h"

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Command/RenameNodeCommand.hpp"
#include "Runtime/Engine.hpp"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include <imgui_stdlib.h>

namespace Editor
{
    namespace
    {
        bool ContainsNodeInSubtree(const Assets::Node& node, uint32_t targetId)
        {
            if (targetId == InvalidId)
            {
                return false;
            }

            if (node.GetInstanceId() == targetId)
            {
                return true;
            }

            for (const auto& child : node.Children())
            {
                if (ContainsNodeInSubtree(*child, targetId))
                {
                    return true;
                }
            }
            return false;
        }

        void DrawNode(EditorContext& ctx, Assets::Node& node, uint32_t& renameTargetId,
                      std::string& renameBuffer, bool& openRenamePopup, bool& focusRenameInput,
                      uint32_t& hoveredIdCandidate, bool autoScrollEnabled, uint32_t& pendingScrollTargetId)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            const bool selected = ctx.scene.IsSelected(node.GetInstanceId());
            const bool locked = ctx.scene.IsLocked(node.GetInstanceId());
            ImGuiTreeNodeFlags flag = ImGuiTreeNodeFlags_FramePadding |
                ImGuiTreeNodeFlags_OpenOnArrow |      // Only expand on arrow click
                ImGuiTreeNodeFlags_SpanAvailWidth |   // Make the whole row clickable
                (selected ? ImGuiTreeNodeFlags_Selected : 0) |
                (node.Children().empty() ? ImGuiTreeNodeFlags_Leaf : 0);

            ImGui::PushID(static_cast<int>(node.GetInstanceId()));
            ImGui::PushStyleColor(ImGuiCol_Text, selected ? ActiveColor : ImGui::GetColorU32(ImGuiCol_Text));
            auto render = node.GetComponent<Runtime::RenderComponent>();
            const int modelId = render ? render->GetModelId() : -1;

            const bool shouldOpenForTarget =
                autoScrollEnabled && pendingScrollTargetId != InvalidId &&
                ContainsNodeInSubtree(node, pendingScrollTargetId);
            if (shouldOpenForTarget)
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }

            const std::string lockPrefix = locked ? std::string(ICON_FA_LOCK " ") : "";
            const std::string label = lockPrefix + (modelId == -1 ? ICON_FA_CIRCLE_NOTCH : ICON_FA_CUBE) +
                std::string(" ") + node.GetName();
            const bool opened = ImGui::TreeNodeEx(label.c_str(), flag);

            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered())
            {
                hoveredIdCandidate = node.GetInstanceId();
            }

            if (autoScrollEnabled && node.GetInstanceId() == pendingScrollTargetId)
            {
                ImGui::SetScrollHereY(0.35f);
                pendingScrollTargetId = InvalidId;
            }

            // Single click to select
            if (!locked && ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            {
                const ImGuiIO& io = ImGui::GetIO();
                const bool toggleSelection = io.KeyCtrl || io.KeySuper;
                if (toggleSelection)
                {
                    ctx.scene.ToggleSelection(node.GetInstanceId());
                }
                else
                {
                    ctx.scene.SetSelectedId(node.GetInstanceId());
                }
            }

            // Double-click to focus camera on the node
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                const ImGuiIO& io = ImGui::GetIO();
                if (!locked && (io.KeyCtrl || io.KeySuper))
                {
                    ctx.scene.AddToSelection(node.GetInstanceId());
                }
                else if (!locked)
                {
                    ctx.scene.SetSelectedId(node.GetInstanceId());
                }
                ctx.actions.Dispatch(ctx, EEditorAction::Camera_FocusSelected,
                                     std::to_string(node.GetInstanceId()));
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("Rename..."))
                {
                    renameTargetId = node.GetInstanceId();
                    renameBuffer = node.GetName();
                    openRenamePopup = true;
                    focusRenameInput = true;
                }
                if (ImGui::MenuItem(locked ? "Unlock" : "Lock"))
                {
                    ctx.scene.ToggleLocked(node.GetInstanceId());
                }
                ImGui::EndPopup();
            }

            if (opened)
            {
                for (auto& child : node.Children())
                {
                    DrawNode(ctx, *child, renameTargetId, renameBuffer, openRenamePopup, focusRenameInput,
                             hoveredIdCandidate, autoScrollEnabled, pendingScrollTargetId);
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }
    } // namespace

    void DrawOutlinerPanel(EditorContext& ctx, EditorUiState& ui)
    {
        static uint32_t renameTargetId = InvalidId;
        static std::string renameBuffer;
        static bool openRenamePopup = false;
        static bool focusRenameInput = false;
        static bool prevAutoScrollEnabled = true;
        static uint32_t lastSelectionId = InvalidId;
        static uint32_t pendingScrollTargetId = InvalidId;
        uint32_t hoveredIdCandidate = InvalidId;

        ImGui::Begin("Outliner", nullptr);
        {
            ImGui::TextDisabled("NOTE");
            ImGui::SameLine();
            utils::HelpMarker("ALL SCENE NODES\n"
                              "limited to 1000 nodes\n"
                              "select and view node properties\n");
            ImGui::Separator();

            ImGui::Text("Nodes");
            ImGui::SameLine();
            const bool autoScrollWasEnabled = ui.outlinerAutoScrollToSelection;
            if (autoScrollWasEnabled)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.35f, 0.75f, 0.75f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.42f, 0.82f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.30f, 0.68f, 0.95f));
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
            if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS "##AutoScrollToSelection"))
            {
                ui.outlinerAutoScrollToSelection = !ui.outlinerAutoScrollToSelection;
            }
            ImGui::PopStyleVar();
            if (autoScrollWasEnabled)
            {
                ImGui::PopStyleColor(3);
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Auto Scroll To Selection: %s\nWhen enabled, selecting an object in viewport "
                                  "auto-scrolls Outliner to it.",
                                  ui.outlinerAutoScrollToSelection ? "On" : "Off");
            }
            ImGui::Separator();

            const uint32_t currentSelectionId = ctx.scene.GetSelectedId();
            if (ui.outlinerAutoScrollToSelection)
            {
                const bool toggledOn = !prevAutoScrollEnabled;
                const bool selectionChanged = currentSelectionId != lastSelectionId;
                if (currentSelectionId != InvalidId && (toggledOn || selectionChanged))
                {
                    pendingScrollTargetId = currentSelectionId;
                }
            }
            else
            {
                pendingScrollTargetId = InvalidId;
            }
            prevAutoScrollEnabled = ui.outlinerAutoScrollToSelection;
            lastSelectionId = currentSelectionId;

            ImGui::BeginChild("ListBox", ImVec2(0, -50));

            if (ImGui::BeginTable("NodesList", 1, ImGuiTableFlags_NoBordersInBodyUntilResize | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("NodeName");
                auto& allnodes = ctx.scene.Nodes();
                uint32_t limit = 1000;
                for (auto& node : allnodes)
                {
                    if (node->GetParent() != nullptr)
                    {
                        continue;
                    }

                    DrawNode(ctx, *node, renameTargetId, renameBuffer, openRenamePopup, focusRenameInput,
                             hoveredIdCandidate, ui.outlinerAutoScrollToSelection, pendingScrollTargetId);

                    if (limit-- <= 0)
                    {
                        break;
                    }
                }
                ImGui::EndTable();
            }

            ImGui::EndChild();

            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows))
            {
                if (hoveredIdCandidate != InvalidId)
                {
                    ctx.scene.SetHoveredId(hoveredIdCandidate);
                }
                else
                {
                    ctx.scene.ClearHoveredId();
                }
            }

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
                ImGui::IsKeyPressed(ImGuiKey_F2, false))
            {
                Assets::Node* selectedNode = ctx.scene.GetNodeByInstanceId(ctx.scene.GetSelectedId());
                if (selectedNode != nullptr)
                {
                    renameTargetId = selectedNode->GetInstanceId();
                    renameBuffer = selectedNode->GetName();
                    openRenamePopup = true;
                    focusRenameInput = true;
                }
            }

            if (openRenamePopup)
            {
                ImGui::OpenPopup("Rename Node");
                openRenamePopup = false;
            }

            if (ImGui::BeginPopupModal("Rename Node", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                Assets::Node* targetNode = ctx.scene.GetNodeByInstanceId(renameTargetId);
                if (targetNode == nullptr)
                {
                    renameTargetId = InvalidId;
                    renameBuffer.clear();
                    focusRenameInput = false;
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    ImGui::Text("Node:");
                    ImGui::SameLine();
                    ImGui::TextUnformatted(targetNode->GetName().c_str());
                    ImGui::Separator();

                    if (focusRenameInput)
                    {
                        ImGui::SetKeyboardFocusHere();
                        focusRenameInput = false;
                    }

                    const bool submitWithEnter =
                        ImGui::InputText("##RenameNodeInput", &renameBuffer, ImGuiInputTextFlags_EnterReturnsTrue);

                    bool shouldSubmit = submitWithEnter;
                    ImGui::SameLine();
                    shouldSubmit = ImGui::Button("OK") || shouldSubmit;
                    ImGui::SameLine();
                    const bool shouldCancel = ImGui::Button("Cancel");

                    if (shouldSubmit)
                    {
                        if (!renameBuffer.empty() && renameBuffer != targetNode->GetName())
                        {
                            ctx.engine.ExecuteCommand(std::make_unique<RenameNodeCommand>(
                                ctx.scene, targetNode->GetInstanceId(), renameBuffer));
                        }

                        renameTargetId = InvalidId;
                        renameBuffer.clear();
                        focusRenameInput = false;
                        ImGui::CloseCurrentPopup();
                    }
                    else if (shouldCancel)
                    {
                        renameTargetId = InvalidId;
                        renameBuffer.clear();
                        focusRenameInput = false;
                        ImGui::CloseCurrentPopup();
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::Spacing();
            ImGui::Text("%d Nodes", static_cast<int>(ctx.scene.Nodes().size()));
            ImGui::Spacing();

            if ((ImGui::GetIO().KeyAlt) && (ImGui::IsKeyPressed(ImGuiKey_F4)))
            {
                ctx.actions.Dispatch(ctx, EEditorAction::System_RequestExit);
                ui.state = false;
            }
        }
        ImGui::End();
    }
} // namespace Editor
