#include "EditorUi.hpp"

#include "EditorActionDispatcher.hpp"
#include "EditorUtils.h"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Command/DeleteNodesCommand.hpp"
#include "Engine/Runtime/Command/RenameNodeCommand.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"

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

        bool SetSubtreeVisibility(Assets::Node& root, bool visible)
        {
            bool changed = false;
            for (const auto& child : root.Children())
            {
                if (auto render = child->GetComponent<Runtime::RenderComponent>())
                {
                    if (render->GetVisible() != visible)
                    {
                        render->SetVisible(visible);
                        changed = true;
                    }
                }
                changed = SetSubtreeVisibility(*child, visible) || changed;
            }
            return changed;
        }

        void CollectOutlinerVisibleIds(Assets::Scene& scene, Assets::Node& node, const ImGuiTextFilter& filter,
                                       bool includeLocked, std::vector<uint32_t>& visibleIds)
        {
            const bool filterActive = filter.IsActive();
            if (filterActive)
            {
                const bool nodePassesFilter = filter.PassFilter(node.GetName().c_str());
                const bool subtreePassesFilter = nodePassesFilter || PassesNodeFilter(node, filter);
                if (!subtreePassesFilter)
                {
                    return;
                }
            }

            if (includeLocked || !scene.IsLocked(node.GetInstanceId()))
            {
                visibleIds.push_back(node.GetInstanceId());
            }
            for (const auto& child : node.Children())
            {
                CollectOutlinerVisibleIds(scene, *child, filter, includeLocked, visibleIds);
            }
        }

        void DrawNode(EditorContext& ctx, EditorUiState& ui, Assets::Node& node, uint32_t& renameTargetId,
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
            if (!node.Children().empty() && node.GetInstanceId() == ui.pendingExpandTargetId)
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                ui.pendingExpandTargetId = InvalidId;
            }
            if (!node.Children().empty() && node.GetInstanceId() == ui.pendingCollapseTargetId)
            {
                ImGui::SetNextItemOpen(false, ImGuiCond_Always);
                ui.pendingCollapseTargetId = InvalidId;
            }

            const std::string label = (modelId == -1 ? ICON_FA_CIRCLE_NOTCH : ICON_FA_CUBE) +
                std::string(" ") + node.GetName();
            const ImU32 textColor = !visible ? ImGui::GetColorU32(ImGuiCol_TextDisabled)
                                             : selected ? ActiveColor : ImGui::GetColorU32(ImGuiCol_Text);
            ImGui::PushStyleColor(ImGuiCol_Text, textColor);
            const bool opened = ImGui::TreeNodeEx(label.c_str(), flag);

            ImGui::PopStyleColor();
            const float columnWidth = ImGui::GetColumnWidth();
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), columnWidth - 56.0f));
            if (render != nullptr)
            {
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
                ImGui::TextDisabled(ICON_FA_EYE);
            }
            ImGui::SameLine(0.0f, 10.0f);
            ImGui::TextUnformatted(locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN);
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                ctx.scene.ToggleLocked(node.GetInstanceId());
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", locked ? "Unlock Node" : "Lock Node");
            }

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
                ImGui::Separator();
                if (ImGui::MenuItem("Hide All Children"))
                {
                    if (SetSubtreeVisibility(node, false))
                    {
                        ctx.scene.MarkDirty();
                    }
                }
                if (ImGui::MenuItem("Show All Children"))
                {
                    if (SetSubtreeVisibility(node, true))
                    {
                        ctx.scene.MarkDirty();
                    }
                }
                ImGui::EndPopup();
            }

            if (opened)
            {
                for (auto& child : node.Children())
                {
                    DrawNode(ctx, ui, *child, renameTargetId, renameBuffer, openRenamePopup, focusRenameInput,
                             hoveredIdCandidate, autoScrollEnabled, pendingScrollTargetId, filter);
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        void DrawLayersPanel()
        {
            struct FLayerRow
            {
                const char* name;
                ImVec4 color;
            };
            constexpr FLayerRow layers[] = {
                {"Default", ImVec4(0.42f, 0.63f, 0.95f, 1.0f)},
                {"Gameplay", ImVec4(0.24f, 0.80f, 0.44f, 1.0f)},
                {"Props", ImVec4(0.95f, 0.68f, 0.24f, 1.0f)},
                {"Colliders", ImVec4(0.88f, 0.28f, 0.28f, 1.0f)},
                {"Lighting", ImVec4(0.80f, 0.70f, 1.0f, 1.0f)},
            };

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            for (const FLayerRow& layer : layers)
            {
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float rowHeight = ImGui::GetFrameHeight();
                drawList->AddCircleFilled(ImVec2(pos.x + 6.0f, pos.y + rowHeight * 0.5f), 4.0f,
                                          ImGui::GetColorU32(layer.color), 12);
                ImGui::Dummy(ImVec2(16.0f, rowHeight));
                ImGui::SameLine();
                ImGui::TextUnformatted(layer.name);
                ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetColumnWidth() - 56.0f));
                ImGui::TextUnformatted(ICON_FA_EYE);
                ImGui::SameLine(0.0f, 10.0f);
                ImGui::TextUnformatted(ICON_FA_LOCK_OPEN);
            }
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
            const std::string subtitle = std::to_string(ctx.scene.Nodes().size()) + " scene nodes";

            NextUI::Theme::IconButton(ICON_FA_PLUS "##CreateActor", "Create Actor (placeholder)", false,
                                         ImVec2(26.0f, 24.0f)); ImGui::SameLine();
            
            const bool autoScrollWasEnabled = ui.outlinerAutoScrollToSelection;
            if (NextUI::Theme::IconButton(ICON_FA_LOCATION_CROSSHAIRS "##AutoScrollToSelection",
                                             "Auto Scroll To Selection", autoScrollWasEnabled,
                                             ImVec2(28.0f, 24.0f)))
            {
                ui.outlinerAutoScrollToSelection = !ui.outlinerAutoScrollToSelection;
            }
            ImGui::SameLine();
            NextUI::Theme::IconButton(ICON_FA_LAYER_GROUP, "Create Group (placeholder)", false,
                                         ImVec2(28.0f, 24.0f));
            nodeFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Search##OutlinerFilter");
            NextUI::Theme::DrawThinSeparator();

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

            ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::Background, 0.42f));
            ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.82f));
            ImGui::BeginChild("ListBox", ImVec2(0, -132.0f), true);

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

                    DrawNode(ctx, ui, *node, renameTargetId, renameBuffer, openRenamePopup, focusRenameInput,
                             hoveredIdCandidate, ui.outlinerAutoScrollToSelection, pendingScrollTargetId, nodeFilter);

                    if (!filterActive && limit-- <= 0)
                    {
                        break;
                    }
                }
                ImGui::EndTable();
            }

            ImGui::EndChild();
            ImGui::PopStyleColor(2);

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
                        ctx.engine.GetCommandHistory().Execute(std::make_unique<Runtime::Command::DeleteNodesCommand>(ctx.scene, std::move(ids)));
                    }
                }

                if (!ImGui::GetIO().WantTextInput)
                {
                    const ImGuiIO& io = ImGui::GetIO();
                    const bool navigateUp = ImGui::IsKeyPressed(ImGuiKey_UpArrow, false);
                    const bool navigateDown = ImGui::IsKeyPressed(ImGuiKey_DownArrow, false);
                    const bool expandSelected = ImGui::IsKeyPressed(ImGuiKey_RightArrow, false);
                    const bool collapseSelected = ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false);

                    if (navigateUp || navigateDown)
                    {
                        std::vector<uint32_t> visibleIds;
                        for (auto& node : ctx.scene.Nodes())
                        {
                            if (node->GetParent() != nullptr) continue;
                            CollectOutlinerVisibleIds(ctx.scene, *node, nodeFilter, false, visibleIds);
                        }

                        if (!visibleIds.empty())
                        {
                            const uint32_t currentId = ctx.scene.GetSelectedId();
                            int currentIndex = -1;
                            for (size_t i = 0; i < visibleIds.size(); ++i)
                            {
                                if (visibleIds[i] == currentId)
                                {
                                    currentIndex = static_cast<int>(i);
                                    break;
                                }
                            }

                            int newIndex = currentIndex;
                            if (navigateUp && currentIndex > 0)
                            {
                                newIndex = currentIndex - 1;
                            }
                            else if (navigateDown && currentIndex < static_cast<int>(visibleIds.size()) - 1)
                            {
                                newIndex = currentIndex == -1 ? 0 : currentIndex + 1;
                            }
                            else if (currentIndex == -1)
                            {
                                newIndex = 0;
                            }

                            if (newIndex != currentIndex && newIndex >= 0 &&
                                newIndex < static_cast<int>(visibleIds.size()))
                            {
                                ctx.scene.SetSelectedId(visibleIds[newIndex]);
                                pendingScrollTargetId = visibleIds[newIndex];
                            }
                        }
                    }

                    if (expandSelected || collapseSelected)
                    {
                        Assets::Node* selectedNode = ctx.scene.GetNodeByInstanceId(ctx.scene.GetSelectedId());
                        if (selectedNode != nullptr && !selectedNode->Children().empty())
                        {
                            if (expandSelected)
                            {
                                ui.pendingExpandTargetId = selectedNode->GetInstanceId();
                                ui.pendingCollapseTargetId = InvalidId;
                            }
                            else
                            {
                                ui.pendingCollapseTargetId = selectedNode->GetInstanceId();
                                ui.pendingExpandTargetId = InvalidId;
                            }
                        }
                    }

                    if ((io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_A, false))
                    {
                        std::vector<uint32_t> visibleIds;
                        for (auto& node : ctx.scene.Nodes())
                        {
                            if (node->GetParent() != nullptr) continue;
                            CollectOutlinerVisibleIds(ctx.scene, *node, nodeFilter, true, visibleIds);
                        }
                        ctx.scene.SetSelection(visibleIds);
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
                            ctx.engine.GetCommandHistory().Execute(std::make_unique<Runtime::Command::RenameNodeCommand>(
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

            if (ImGui::BeginTabBar("OutlinerSubTabs"))
            {
                if (ImGui::BeginTabItem("Scene"))
                {
                    ImGui::TextDisabled("Root scene graph");
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Layers"))
                {
                    DrawLayersPanel();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            NextUI::Theme::DrawThinSeparator(0.65f);
            ImGui::Text("%d actors (%d selected)", static_cast<int>(ctx.scene.Nodes().size()),
                        static_cast<int>(ctx.scene.GetSelectedIds().size()));

            if ((ImGui::GetIO().KeyAlt) && (ImGui::IsKeyPressed(ImGuiKey_F4)))
            {
                ctx.actions.Dispatch(ctx, EEditorAction::System_RequestExit);
                ui.state = false;
            }
        }
        ImGui::End();
    }
} // namespace Editor
