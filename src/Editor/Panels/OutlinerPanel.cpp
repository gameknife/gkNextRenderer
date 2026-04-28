#include "Editor/EditorUi.hpp"

#include "Editor/EditorActionDispatcher.hpp"
#include "Editor/EditorUtils.h"

#include "Assets/Core/Node.h"
#include "Assets/Core/Scene.hpp"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Command/DeleteNodesCommand.hpp"
#include "Runtime/Command/RenameNodeCommand.hpp"
#include "Runtime/Engine.hpp"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include <imgui_stdlib.h>

#include <spdlog/spdlog.h>

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

        bool PassesNodeFilter(const Assets::Node& node, const ImGuiTextFilter& filter)
        {
            if (filter.PassFilter(node.GetName().c_str()))
            {
                return true;
            }

            for (const auto& child : node.Children())
            {
                if (PassesNodeFilter(*child, filter))
                {
                    return true;
                }
            }
            return false;
        }

        void AlignNextOutlinerInlineItem()
        {
            ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
            ImGui::AlignTextToFramePadding();
        }

        std::string MakeNodePath(Assets::Node& node)
        {
            std::string path = node.GetName();
            Assets::Node* parent = node.GetParent();
            while (parent != nullptr)
            {
                path = parent->GetName() + "/" + path;
                parent = parent->GetParent();
            }
            return path;
        }

        void DrawNode(EditorContext& ctx, Assets::Node& node, uint32_t& renameTargetId,
                      std::string& renameBuffer, bool& openRenamePopup, bool& focusRenameInput,
                      uint32_t& hoveredIdCandidate, bool autoScrollEnabled, uint32_t& pendingScrollTargetId,
                      const ImGuiTextFilter& filter)
        {
            const bool filterActive = filter.IsActive();
            const bool nodePassesFilter = !filterActive || filter.PassFilter(node.GetName().c_str());
            const bool subtreePassesFilter = !filterActive || nodePassesFilter || PassesNodeFilter(node, filter);
            if (!subtreePassesFilter)
            {
                return;
            }

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
            auto render = node.GetComponent<Runtime::RenderComponent>();
            const int modelId = render ? render->GetModelId() : -1;
            const bool visible = render == nullptr || render->GetVisible();
            const float visibilityIconWidth = ImGui::CalcTextSize(ICON_FA_EYE).x;

            const bool shouldOpenForTarget =
                autoScrollEnabled && pendingScrollTargetId != InvalidId &&
                ContainsNodeInSubtree(node, pendingScrollTargetId);
            if (shouldOpenForTarget)
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            if (filterActive && !node.Children().empty())
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }

            const std::string lockPrefix = locked ? std::string(ICON_FA_LOCK " ") : "";
            const std::string label = lockPrefix + (modelId == -1 ? ICON_FA_CIRCLE_NOTCH : ICON_FA_CUBE) +
                std::string(" ") + node.GetName();
            if (render != nullptr)
            {
                ImGui::AlignTextToFramePadding();
                if (!visible)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                }
                ImGui::TextUnformatted(visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH);
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                {
                    render->SetVisible(!visible);
                    ctx.scene.MarkDirty();
                }
                if (!visible)
                {
                    ImGui::PopStyleColor();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("%s", visible ? "Hide Node" : "Show Node");
                }
            }
            else
            {
                ImGui::Dummy(ImVec2(visibilityIconWidth, ImGui::GetFrameHeight()));
            }
            AlignNextOutlinerInlineItem();

            const ImU32 textColor = !visible ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                             : selected ? ActiveColor : ImGui::GetColorU32(ImGuiCol_Text);
            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
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
                // Ensure Outliner gets keyboard focus so shortcuts (Delete, F2, arrows) work immediately
                ImGui::SetWindowFocus(nullptr);
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
                ImGui::Separator();
                if (ImGui::MenuItem("Copy Node Name"))
                {
                    ImGui::SetClipboardText(node.GetName().c_str());
                    SPDLOG_INFO("Copied node name: {}", node.GetName());
                }
                if (ImGui::MenuItem("Copy Node Path"))
                {
                    const std::string nodePath = MakeNodePath(node);
                    ImGui::SetClipboardText(nodePath.c_str());
                    SPDLOG_INFO("Copied node path: {}", nodePath);
                }
                ImGui::EndPopup();
            }

            if (opened)
            {
                for (auto& child : node.Children())
                {
                    DrawNode(ctx, *child, renameTargetId, renameBuffer, openRenamePopup, focusRenameInput,
                             hoveredIdCandidate, autoScrollEnabled, pendingScrollTargetId, filter);
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
        static ImGuiTextFilter nodeFilter;
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
            ImGui::SameLine();
            ImGui::SetNextItemWidth(200.0f);
            nodeFilter.Draw(ICON_FA_MAGNIFYING_GLASS "##OutlinerFilter", 200.0f);
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
                const bool filterActive = nodeFilter.IsActive();
                uint32_t limit = 1000;
                for (auto& node : allnodes)
                {
                    if (node->GetParent() != nullptr)
                    {
                        continue;
                    }

                    DrawNode(ctx, *node, renameTargetId, renameBuffer, openRenamePopup, focusRenameInput,
                             hoveredIdCandidate, ui.outlinerAutoScrollToSelection, pendingScrollTargetId, nodeFilter);

                    if (!filterActive && limit-- <= 0)
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

            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
            {
                if (ImGui::IsKeyPressed(ImGuiKey_F2, false))
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

                // Delete / Backspace: remove selected nodes (works while Outliner has keyboard focus)
                if (!ImGui::GetIO().WantTextInput &&
                    (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                     ImGui::IsKeyPressed(ImGuiKey_Backspace, false)))
                {
                    std::vector<uint32_t> ids = ctx.scene.GetSelectedIds();
                    if (!ids.empty())
                    {
                        ctx.engine.ExecuteCommand(std::make_unique<DeleteNodesCommand>(ctx.scene, std::move(ids)));
                    }
                }

                // Arrow key navigation
                if (!ImGui::GetIO().WantTextInput)
                {
                    bool navigateUp = ImGui::IsKeyPressed(ImGuiKey_UpArrow, false);
                    bool navigateDown = ImGui::IsKeyPressed(ImGuiKey_DownArrow, false);

                    if (navigateUp || navigateDown)
                    {
                        const bool filterActive = nodeFilter.IsActive();
                        std::vector<uint32_t> visibleIds;
                        std::function<void(Assets::Node&)> collectVisible = [&](Assets::Node& node)
                        {
                            if (filterActive)
                            {
                                bool nodePassesFilter = nodeFilter.PassFilter(node.GetName().c_str());
                                bool subtreePassesFilter = nodePassesFilter || PassesNodeFilter(node, nodeFilter);
                                if (!subtreePassesFilter) return;
                            }
                            if (!ctx.scene.IsLocked(node.GetInstanceId()))
                                visibleIds.push_back(node.GetInstanceId());
                            for (auto& child : node.Children())
                                collectVisible(*child);
                        };

                        for (auto& node : ctx.scene.Nodes())
                        {
                            if (node->GetParent() != nullptr) continue;
                            if (filterActive && !PassesNodeFilter(*node, nodeFilter)) continue;
                            collectVisible(*node);
                        }

                        if (!visibleIds.empty())
                        {
                            uint32_t currentId = ctx.scene.GetSelectedId();
                            int idx = -1;
                            for (size_t i = 0; i < visibleIds.size(); ++i)
                            {
                                if (visibleIds[i] == currentId)
                                {
                                    idx = static_cast<int>(i);
                                    break;
                                }
                            }

                            int newIdx = idx;
                            if (navigateUp && idx > 0)
                                newIdx = idx - 1;
                            else if (navigateDown && idx < static_cast<int>(visibleIds.size()) - 1)
                                newIdx = (idx == -1) ? 0 : idx + 1;
                            else if (idx == -1 && !visibleIds.empty())
                                newIdx = 0;

                            if (newIdx != idx && newIdx >= 0 && newIdx < static_cast<int>(visibleIds.size()))
                            {
                                ctx.scene.SetSelectedId(visibleIds[newIdx]);
                                pendingScrollTargetId = visibleIds[newIdx];
                            }
                        }
                    }
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
