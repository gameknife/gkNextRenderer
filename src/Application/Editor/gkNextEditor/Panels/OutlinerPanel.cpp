#include "EditorUi.hpp"

#include "EditorActionDispatcher.hpp"
#include "EditorUtils.h"

#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SceneReferenceComponent.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/RenameNodeCommand.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include <imgui_stdlib.h>

#include <spdlog/spdlog.h>

namespace Editor
{
    namespace
    {
        // Width of the visibility / lock columns; the hit box matches the column so the icon
        // highlight and the click target line up.
        constexpr float kOutlinerToggleCellWidth = 18.0f;

        bool ContainsNodeInSubtree(const Assets::Node& node, uint32_t targetId)
        {
            if (node.IsSceneReferenceInternal())
            {
                return false;
            }
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
                if (child->IsSceneReferenceInternal())
                {
                    continue;
                }
                if (ContainsNodeInSubtree(*child, targetId))
                {
                    return true;
                }
            }
            return false;
        }

        bool PassesNodeFilter(const Assets::Node& node, const ImGuiTextFilter& filter)
        {
            if (node.IsSceneReferenceInternal())
            {
                return false;
            }
            if (filter.PassFilter(node.GetName().c_str()))
            {
                return true;
            }
            if (auto sceneReference = node.GetComponent<Runtime::SceneReferenceComponent>())
            {
                if (filter.PassFilter(sceneReference->GetAssetPath().c_str()))
                {
                    return true;
                }
            }

            for (const auto& child : node.Children())
            {
                if (child->IsSceneReferenceInternal())
                {
                    continue;
                }
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

        int GetNodeDepth(Assets::Node& node)
        {
            int depth = 0;
            Assets::Node* parent = node.GetParent();
            while (parent != nullptr)
            {
                ++depth;
                parent = parent->GetParent();
            }
            return depth;
        }

        void UndoOutlinerIndentForCurrentColumn(float indentWidth)
        {
            if (indentWidth <= 0.0f)
            {
                return;
            }

            ImGui::SetCursorPosX(std::max(0.0f, ImGui::GetCursorPosX() - indentWidth));
        }

        void ApplyOutlinerIndentForCurrentColumn(float indentWidth)
        {
            if (indentWidth <= 0.0f)
            {
                return;
            }

            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indentWidth);
        }

        bool SetSubtreeVisibility(Assets::Node& root, bool visible)
        {
            bool changed = false;
            if (auto render = root.GetComponent<Runtime::RenderComponent>())
            {
                if (render->GetVisible() != visible)
                {
                    render->SetVisible(visible);
                    changed = true;
                }
            }
            for (const auto& child : root.Children())
            {
                changed = SetSubtreeVisibility(*child, visible) || changed;
            }
            return changed;
        }

        bool IsSubtreeVisible(const Assets::Node& root)
        {
            bool hasDrawable = false;
            bool allVisible = true;
            if (auto render = root.GetComponent<Runtime::RenderComponent>())
            {
                hasDrawable = true;
                allVisible = allVisible && render->GetVisible();
            }
            for (const auto& child : root.Children())
            {
                if (auto render = child->GetComponent<Runtime::RenderComponent>())
                {
                    hasDrawable = true;
                    allVisible = allVisible && render->GetVisible();
                }
                allVisible = IsSubtreeVisible(*child) && allVisible;
            }
            return !hasDrawable || allVisible;
        }

        struct FLayerStats
        {
            std::string Name;
            uint32_t NodeCount = 0;
            uint32_t DrawableCount = 0;
            uint32_t VisibleDrawableCount = 0;
            uint32_t LockedCount = 0;
        };

        std::string NormalizedLayerName(const Assets::Node& node)
        {
            return node.GetLayer().empty() ? "Default" : node.GetLayer();
        }

        ImVec4 LayerColor(const std::string& layer)
        {
            if (layer == "Default") return ImVec4(0.42f, 0.63f, 0.95f, 1.0f);
            if (layer == "Gameplay") return ImVec4(0.24f, 0.80f, 0.44f, 1.0f);
            if (layer == "Props") return ImVec4(0.95f, 0.68f, 0.24f, 1.0f);
            if (layer == "Colliders") return ImVec4(0.88f, 0.28f, 0.28f, 1.0f);
            if (layer == "Lighting") return ImVec4(0.80f, 0.70f, 1.0f, 1.0f);

            const uint32_t hash = static_cast<uint32_t>(std::hash<std::string>{}(layer));
            const float hue = static_cast<float>(hash % 360u) / 360.0f;
            ImVec4 color;
            ImGui::ColorConvertHSVtoRGB(hue, 0.56f, 0.92f, color.x, color.y, color.z);
            color.w = 1.0f;
            return color;
        }

        std::vector<FLayerStats> CollectLayerStats(Assets::Scene& scene)
        {
            std::map<std::string, FLayerStats> layers;
            layers["Default"].Name = "Default";
            for (const auto& node : scene.Nodes())
            {
                if (node->IsSceneReferenceInternal())
                {
                    continue;
                }
                const std::string layerName = NormalizedLayerName(*node);
                FLayerStats& stats = layers[layerName];
                stats.Name = layerName;
                stats.NodeCount++;
                if (scene.IsLocked(node->GetInstanceId()))
                {
                    stats.LockedCount++;
                }

                auto render = node->GetComponent<Runtime::RenderComponent>();
                if (render != nullptr)
                {
                    stats.DrawableCount++;
                    if (render->GetVisible())
                    {
                        stats.VisibleDrawableCount++;
                    }
                }
            }

            std::vector<FLayerStats> result;
            result.reserve(layers.size());
            for (auto& [name, stats] : layers)
            {
                result.push_back(std::move(stats));
            }
            return result;
        }

        bool SetLayerVisibility(Assets::Scene& scene, const std::string& layerName, bool visible)
        {
            bool changed = false;
            for (const auto& node : scene.Nodes())
            {
                if (node->IsSceneReferenceInternal())
                {
                    continue;
                }
                if (NormalizedLayerName(*node) != layerName)
                {
                    continue;
                }

                auto render = node->GetComponent<Runtime::RenderComponent>();
                if (render != nullptr && render->GetVisible() != visible)
                {
                    render->SetVisible(visible);
                    changed = true;
                }
            }
            if (changed)
            {
                scene.MarkDirty();
            }
            return changed;
        }

        void SetLayerLocked(Assets::Scene& scene, const std::string& layerName, bool locked)
        {
            for (const auto& node : scene.Nodes())
            {
                if (node->IsSceneReferenceInternal())
                {
                    continue;
                }
                if (NormalizedLayerName(*node) == layerName)
                {
                    scene.SetLocked(node->GetInstanceId(), locked);
                }
            }
        }

        std::vector<uint32_t> CollectLayerNodeIds(Assets::Scene& scene, const std::string& layerName, bool includeLocked)
        {
            std::vector<uint32_t> ids;
            for (const auto& node : scene.Nodes())
            {
                if (node->IsSceneReferenceInternal())
                {
                    continue;
                }
                if (NormalizedLayerName(*node) != layerName)
                {
                    continue;
                }
                if (includeLocked || !scene.IsLocked(node->GetInstanceId()))
                {
                    ids.push_back(node->GetInstanceId());
                }
            }
            return ids;
        }

        void CollectOutlinerVisibleIds(Assets::Scene& scene, Assets::Node& node, const ImGuiTextFilter& filter,
                                       bool includeLocked, std::vector<uint32_t>& visibleIds)
        {
            if (node.IsSceneReferenceInternal())
            {
                return;
            }
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
                if (child->IsSceneReferenceInternal())
                {
                    continue;
                }
                CollectOutlinerVisibleIds(scene, *child, filter, includeLocked, visibleIds);
            }
        }

        // Row toggle (visibility, lock) drawn as an icon over a full-cell hit box, so the highlight
        // and the clickable area are the same rectangle.
        bool RowToggleIcon(const char* icon, NextUI::Theme::EColor tint, float tintAlpha, float cellWidth,
                           const char* tooltip, bool& outHovered)
        {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const float height = ImGui::GetFrameHeight();
            ImGui::InvisibleButton(icon, ImVec2(cellWidth, height));
            const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            outHovered = ImGui::IsItemHovered();
            if (outHovered && tooltip != nullptr)
            {
                ImGui::SetTooltip("%s", tooltip);
            }

            const ImU32 color = outHovered
                ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Text)
                : NextUI::Theme::ColorU32(tint, tintAlpha);
            const ImVec2 iconSize = ImGui::CalcTextSize(icon);
            ImGui::GetWindowDrawList()->AddText(
                origin + ImVec2(std::floor((cellWidth - iconSize.x) * 0.5f),
                                std::floor((height - iconSize.y) * 0.5f)),
                color, icon);
            return clicked;
        }

        void DrawNode(EditorContext& ctx, EditorUiState& ui, Assets::Node& node, uint32_t& renameTargetId,
                      std::string& renameBuffer, bool& openRenamePopup, bool& focusRenameInput,
                      uint32_t& hoveredIdCandidate, bool autoScrollEnabled, uint32_t& pendingScrollTargetId,
                      bool& suppressNextSelectionAutoScroll, const ImGuiTextFilter& filter)
        {
            if (!SubtreeMatchesKind(node, ui.outliner.typeFilter))
            {
                return;
            }

            const bool filterActive = filter.IsActive();
            const bool nodePassesFilter = !filterActive || filter.PassFilter(node.GetName().c_str());
            const bool subtreePassesFilter = !filterActive || nodePassesFilter || PassesNodeFilter(node, filter);
            if (!subtreePassesFilter)
            {
                return;
            }

            const int depth = GetNodeDepth(node);
            const float indentWidth = static_cast<float>(depth) * ImGui::GetStyle().IndentSpacing;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            UndoOutlinerIndentForCurrentColumn(indentWidth);

            const bool selected = ctx.scene.IsSelected(node.GetInstanceId());
            const bool locked = ctx.scene.IsLocked(node.GetInstanceId());
            ImGui::PushID(static_cast<int>(node.GetInstanceId()));

            auto sceneReference = node.GetComponent<Runtime::SceneReferenceComponent>();
            auto render = node.GetComponent<Runtime::RenderComponent>();
            const bool visible = sceneReference ? IsSubtreeVisible(node) : (render == nullptr || render->GetVisible());

            bool cellHovered = false;

            // 1. Column 0: Visibility
            if (render != nullptr || sceneReference)
            {
                const bool clicked = RowToggleIcon(
                    visible ? ICON_FA_EYE : ICON_FA_EYE_SLASH,
                    visible ? NextUI::Theme::EColor::TextMuted : NextUI::Theme::EColor::Danger,
                    visible ? 0.50f : 0.75f, kOutlinerToggleCellWidth,
                    visible ? "Click to hide node" : "Click to show node", cellHovered);
                if (clicked)
                {
                    if (sceneReference)
                    {
                        if (SetSubtreeVisibility(node, !visible))
                        {
                            ctx.scene.MarkDirty();
                        }
                    }
                    else
                    {
                        render->SetVisible(!visible);
                        ctx.scene.MarkDirty();
                    }
                }
            }
            else
            {
                RowToggleIcon(ICON_FA_EYE, NextUI::Theme::EColor::TextDim, 0.35f, kOutlinerToggleCellWidth,
                              nullptr, cellHovered);
            }
            if (cellHovered)
            {
                hoveredIdCandidate = node.GetInstanceId();
            }

            // 2. Column 1: Lock
            ImGui::TableSetColumnIndex(1);
            if (RowToggleIcon(locked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN,
                              locked ? NextUI::Theme::EColor::Warning : NextUI::Theme::EColor::TextDim,
                              locked ? 0.85f : 0.30f, kOutlinerToggleCellWidth,
                              locked ? "Click to unlock node" : "Click to lock node", cellHovered))
            {
                ctx.scene.ToggleLocked(node.GetInstanceId());
            }
            if (cellHovered)
            {
                hoveredIdCandidate = node.GetInstanceId();
            }

            // 3. Column 2: Node Name with custom Visual Icon and Guide Lines
            ImGui::TableSetColumnIndex(2);
            ApplyOutlinerIndentForCurrentColumn(indentWidth);

            ImGuiTreeNodeFlags flag = ImGuiTreeNodeFlags_FramePadding |
                ImGuiTreeNodeFlags_OpenOnArrow |      // Only expand on arrow click
                ImGuiTreeNodeFlags_SpanAvailWidth |   // Make the whole row clickable
                (selected ? ImGuiTreeNodeFlags_Selected : 0) |
                ((node.Children().empty() || sceneReference) ? ImGuiTreeNodeFlags_Leaf : 0);

            const bool shouldOpenForTarget =
                autoScrollEnabled && pendingScrollTargetId != InvalidId &&
                ContainsNodeInSubtree(node, pendingScrollTargetId);
            if (shouldOpenForTarget)
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            if (ui.outliner.expandAllRequested)
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            if (ui.outliner.collapseAllRequested)
            {
                ImGui::SetNextItemOpen(false, ImGuiCond_Always);
            }
            if (filterActive && !node.Children().empty() && !sceneReference)
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            if (!node.Children().empty() && !sceneReference && node.GetInstanceId() == ui.pendingExpandTargetId)
            {
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                ui.pendingExpandTargetId = InvalidId;
            }
            if (!node.Children().empty() && !sceneReference && node.GetInstanceId() == ui.pendingCollapseTargetId)
            {
                ImGui::SetNextItemOpen(false, ImGuiCond_Always);
                ui.pendingCollapseTargetId = InvalidId;
            }

            const FNodeVisual& visual = ClassifyNode(node);
            const std::string label = fmt::format("{}  {}###Node_{}", visual.icon, node.GetName(), node.GetInstanceId());

            const ImVec4 textVecColor = !visible ? NextUI::Theme::Color(NextUI::Theme::EColor::TextDim)
                                                 : selected ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f)
                                                            : NextUI::Theme::Color(NextUI::Theme::EColor::Text);
            ImGui::PushStyleColor(ImGuiCol_Text, textVecColor);
            const bool opened = ImGui::TreeNodeEx(label.c_str(), flag);
            const bool treeNodeHovered = ImGui::IsItemHovered();
            const bool treeNodeClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool treeNodeDoubleClicked = treeNodeHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
            const bool treeNodeToggledOpen = ImGui::IsItemToggledOpen();

            ImGui::PopStyleColor();

            // Draw left accent bar for selected row
            if (selected)
            {
                NextUI::Theme::DrawSelectionAccentStrip(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            }

            // Draw indentation guide lines
            if (depth > 0)
            {
                const float indentStep = ImGui::GetStyle().IndentSpacing;
                const ImVec2 rMin = ImGui::GetItemRectMin();
                const ImVec2 rMax = ImGui::GetItemRectMax();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                const ImU32 lineCol = NextUI::Theme::ColorU32(NextUI::Theme::EColor::Border, 0.28f);
                for (int d = 1; d <= depth; ++d)
                {
                    const float lineX = rMin.x - (static_cast<float>(depth - d + 1) * indentStep) + 8.0f;
                    dl->AddLine(ImVec2(lineX, rMin.y), ImVec2(lineX, rMax.y), lineCol, 1.0f);
                }
            }

            if (treeNodeHovered)
            {
                hoveredIdCandidate = node.GetInstanceId();
            }

            if (autoScrollEnabled && node.GetInstanceId() == pendingScrollTargetId)
            {
                ImGui::SetScrollHereY(0.35f);
                pendingScrollTargetId = InvalidId;
            }

            // Single click to select
            if (!locked && treeNodeClicked && !treeNodeToggledOpen)
            {
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
                suppressNextSelectionAutoScroll = true;
            }

            // Double-click to focus camera on the node
            if (treeNodeDoubleClicked)
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
                suppressNextSelectionAutoScroll = true;
                ctx.actions.Dispatch(ctx, EEditorAction::Camera_FocusSelected,
                                     std::to_string(node.GetInstanceId()));
            }

            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem(ICON_FA_PEN "  Rename...", "F2"))
                {
                    renameTargetId = node.GetInstanceId();
                    renameBuffer = node.GetName();
                    openRenamePopup = true;
                    focusRenameInput = true;
                }
                if (ImGui::MenuItem(locked ? ICON_FA_LOCK_OPEN "  Unlock" : ICON_FA_LOCK "  Lock"))
                {
                    ctx.scene.ToggleLocked(node.GetInstanceId());
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_COPY "  Copy Node Name"))
                {
                    ImGui::SetClipboardText(node.GetName().c_str());
                    SPDLOG_INFO("Copied node name: {}", node.GetName());
                }
                if (ImGui::MenuItem(ICON_FA_ROUTE "  Copy Node Path"))
                {
                    const std::string nodePath = MakeNodePath(node);
                    ImGui::SetClipboardText(nodePath.c_str());
                    SPDLOG_INFO("Copied node path: {}", nodePath);
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_EYE_SLASH "  Hide All Children"))
                {
                    if (SetSubtreeVisibility(node, false))
                    {
                        ctx.scene.MarkDirty();
                    }
                }
                if (ImGui::MenuItem(ICON_FA_EYE "  Show All Children"))
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
                if (!sceneReference)
                {
                    for (auto& child : node.Children())
                    {
                        if (child->IsSceneReferenceInternal())
                        {
                            continue;
                        }
                        DrawNode(ctx, ui, *child, renameTargetId, renameBuffer, openRenamePopup, focusRenameInput,
                                 hoveredIdCandidate, autoScrollEnabled, pendingScrollTargetId,
                                 suppressNextSelectionAutoScroll, filter);
                    }
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }

        void DrawLayersPanel(EditorContext& ctx)
        {
            std::vector<FLayerStats> layers = CollectLayerStats(ctx.scene);
            ImDrawList* drawList = ImGui::GetWindowDrawList();

            if (ImGui::BeginTable("LayerList", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBodyUntilResize))
            {
                ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 42.0f);
                ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed, 28.0f);
                ImGui::TableSetupColumn("Locked", ImGuiTableColumnFlags_WidthFixed, 28.0f);
                ImGui::TableSetupColumn("Select", ImGuiTableColumnFlags_WidthFixed, 28.0f);

                for (const FLayerStats& layer : layers)
                {
                    const bool allDrawableVisible =
                        layer.DrawableCount == 0 || layer.VisibleDrawableCount == layer.DrawableCount;
                    const bool allLocked = layer.NodeCount > 0 && layer.LockedCount == layer.NodeCount;
                    const bool partiallyVisible =
                        layer.DrawableCount > 0 && layer.VisibleDrawableCount > 0 &&
                        layer.VisibleDrawableCount < layer.DrawableCount;
                    const bool partiallyLocked = layer.LockedCount > 0 && !allLocked;

                    ImGui::PushID(layer.Name.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);

                    const ImVec2 pos = ImGui::GetCursorScreenPos();
                    const float rowHeight = ImGui::GetFrameHeight();
                    drawList->AddCircleFilled(ImVec2(pos.x + 6.0f, pos.y + rowHeight * 0.5f), 4.0f,
                                              ImGui::GetColorU32(LayerColor(layer.Name)), 12);
                    ImGui::Dummy(ImVec2(16.0f, rowHeight));
                    ImGui::SameLine();
                    ImGui::TextUnformatted(layer.Name.c_str());
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                    {
                        ctx.scene.SetSelection(CollectLayerNodeIds(ctx.scene, layer.Name, false));
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Select layer nodes");
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%u", layer.NodeCount);

                    ImGui::TableSetColumnIndex(2);
                    if (layer.DrawableCount == 0)
                    {
                        ImGui::BeginDisabled();
                    }
                    if (partiallyVisible)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted));
                    }
                    if (ImGui::SmallButton(allDrawableVisible ? ICON_FA_EYE : ICON_FA_EYE_SLASH))
                    {
                        SetLayerVisibility(ctx.scene, layer.Name, !allDrawableVisible);
                    }
                    if (partiallyVisible)
                    {
                        ImGui::PopStyleColor();
                    }
                    if (layer.DrawableCount == 0)
                    {
                        ImGui::EndDisabled();
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", allDrawableVisible ? "Hide layer" : "Show layer");
                    }

                    ImGui::TableSetColumnIndex(3);
                    if (partiallyLocked)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted));
                    }
                    if (ImGui::SmallButton(allLocked ? ICON_FA_LOCK : ICON_FA_LOCK_OPEN))
                    {
                        SetLayerLocked(ctx.scene, layer.Name, !allLocked);
                    }
                    if (partiallyLocked)
                    {
                        ImGui::PopStyleColor();
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("%s", allLocked ? "Unlock layer" : "Lock layer");
                    }

                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::SmallButton(ICON_FA_ARROW_POINTER))
                    {
                        ctx.scene.SetSelection(CollectLayerNodeIds(ctx.scene, layer.Name, false));
                    }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Select layer nodes");
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
    } // namespace

    void DrawOutlinerPanel(EditorContext& ctx, EditorUiState& ui)
    {
        auto& state = ui.outliner;
        uint32_t hoveredIdCandidate = InvalidId;

        ImGui::Begin("Outliner", nullptr);
        {
            // 1. Top Mode Tabs (Scene vs Layers)
            static constexpr NextUI::Theme::FPillOption kModeOptions[] = {
                {static_cast<int>(EOutlinerTab::SceneGraph), ICON_FA_SITEMAP " Scene Graph"},
                {static_cast<int>(EOutlinerTab::Layers), ICON_FA_LAYER_GROUP " Layers"},
            };
            int currentTab = static_cast<int>(state.currentTab);
            if (NextUI::Theme::SegmentedPills("##OutlinerTopTabs", kModeOptions, currentTab, -1.0f, 24.0f))
            {
                state.currentTab = static_cast<EOutlinerTab>(currentTab);
            }
            ImGui::Dummy(ImVec2(0.0f, 2.0f));

            if (state.currentTab == EOutlinerTab::SceneGraph)
            {
                // Action and Stats row
                const size_t totalNodes = ctx.scene.Nodes().size();
                const size_t selectedCount = ctx.scene.GetSelectedIds().size();
                std::string badgeText = fmt::format("{} nodes", totalNodes);
                if (selectedCount > 0)
                {
                    badgeText += fmt::format(" · {} sel", selectedCount);
                }

                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "%s", badgeText.c_str());

                // Action buttons on the right
                constexpr float btnW = 26.0f;
                constexpr float btnH = 22.0f;
                constexpr float btnSpacing = 3.0f;
                const float rightButtonsWidth = btnW * 4.0f + btnSpacing * 3.0f;
                NextUI::Theme::SameLineRightAligned(rightButtonsWidth, 40.0f);

                const bool autoScrollWasEnabled = ctx.settings.outlinerAutoScroll;
                if (NextUI::Theme::IconButton(ICON_FA_LOCATION_CROSSHAIRS "##AutoScroll",
                                              "Auto Scroll To Selection", autoScrollWasEnabled,
                                              ImVec2(btnW, btnH)))
                {
                    ctx.settings.outlinerAutoScroll = !ctx.settings.outlinerAutoScroll;
                }
                ImGui::SameLine(0.0f, btnSpacing);
                if (NextUI::Theme::IconButton(ICON_FA_FOLDER_OPEN "##ExpandAll", "Expand All Subtrees", false, ImVec2(btnW, btnH)))
                {
                    state.expandAllRequested = true;
                }
                ImGui::SameLine(0.0f, btnSpacing);
                if (NextUI::Theme::IconButton(ICON_FA_FOLDER_CLOSED "##CollapseAll", "Collapse All Subtrees", false, ImVec2(btnW, btnH)))
                {
                    state.collapseAllRequested = true;
                }
                ImGui::SameLine(0.0f, btnSpacing);
                NextUI::Theme::IconButton(ICON_FA_PLUS "##CreateActor", "Create Actor (placeholder)", false, ImVec2(btnW, btnH));

                // Search Filter Input
                ImGui::SetNextItemWidth(-FLT_MIN);
                state.nodeFilter.Draw(ICON_FA_MAGNIFYING_GLASS " Search##OutlinerFilter");

                // Quick Type Filter Pills
                static constexpr NextUI::Theme::FPillOption kTypeOptions[] = {
                    {static_cast<int>(ENodeKind::All), "All"},
                    {static_cast<int>(ENodeKind::Mesh), ICON_FA_CUBE " Mesh"},
                    {static_cast<int>(ENodeKind::Light), ICON_FA_LIGHTBULB " Light"},
                    {static_cast<int>(ENodeKind::Camera), ICON_FA_VIDEO " Cam"},
                };
                int typeFilter = static_cast<int>(state.typeFilter);
                if (NextUI::Theme::SegmentedPills("##OutlinerTypePills", kTypeOptions, typeFilter, -1.0f, 20.0f))
                {
                    state.typeFilter = static_cast<ENodeKind>(typeFilter);
                }
                NextUI::Theme::DrawThinSeparator(0.50f);

                const uint32_t currentSelectionId = ctx.scene.GetSelectedId();
                if (ctx.settings.outlinerAutoScroll)
                {
                    const bool toggledOn = !state.prevAutoScrollEnabled;
                    const bool selectionChanged = currentSelectionId != state.lastSelectionId;
                    if (currentSelectionId != InvalidId && (toggledOn || selectionChanged))
                    {
                        if (!selectionChanged || !state.suppressNextSelectionAutoScroll)
                        {
                            state.pendingScrollTargetId = currentSelectionId;
                        }
                    }
                }
                else
                {
                    state.pendingScrollTargetId = InvalidId;
                }
                state.suppressNextSelectionAutoScroll = false;
                state.prevAutoScrollEnabled = ctx.settings.outlinerAutoScroll;
                state.lastSelectionId = currentSelectionId;

                NextUI::Theme::BeginInsetPanel("ListBox", ImVec2(0.0f, 0.0f), false, 0,
                                               ImGui::GetStyle().WindowPadding, 0.42f);

                if (ImGui::BeginTable("NodesList", 3, ImGuiTableFlags_NoBordersInBodyUntilResize | ImGuiTableFlags_RowBg))
                {
                    ImGui::TableSetupColumn("Visible", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                                            kOutlinerToggleCellWidth);
                    ImGui::TableSetupColumn("Locked", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize,
                                            kOutlinerToggleCellWidth);
                    ImGui::TableSetupColumn("NodeName", ImGuiTableColumnFlags_WidthStretch);

                    auto& allnodes = ctx.scene.Nodes();
                    const bool filterActive = state.nodeFilter.IsActive();
                    uint32_t limit = 1500;
                    for (auto& node : allnodes)
                    {
                        if (node->IsSceneReferenceInternal())
                        {
                            continue;
                        }
                        if (node->GetParent() != nullptr)
                        {
                            continue;
                        }

                        DrawNode(ctx, ui, *node,
                                 state.renameTargetId,
                                 state.renameBuffer,
                                 state.openRenamePopup,
                                 state.focusRenameInput,
                                 hoveredIdCandidate,
                                 ctx.settings.outlinerAutoScroll,
                                 state.pendingScrollTargetId,
                                 state.suppressNextSelectionAutoScroll,
                                 state.nodeFilter);

                        if (!filterActive && limit-- <= 0)
                        {
                            break;
                        }
                    }
                    ImGui::EndTable();
                }

                NextUI::Theme::EndInsetPanel();
            }
            else
            {
                // Layers Tab view: full height
                NextUI::Theme::BeginInsetPanel("LayersBox", ImVec2(0.0f, 0.0f), false, 0,
                                               ImGui::GetStyle().WindowPadding, 0.42f);
                DrawLayersPanel(ctx);
                NextUI::Theme::EndInsetPanel();
            }

            // Consumed once the tree has been walked. Reset unconditionally so a request raised while
            // the Layers tab is up does not fire the next time the Scene Graph tab comes back.
            state.expandAllRequested = false;
            state.collapseAllRequested = false;

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
                        state.renameTargetId = selectedNode->GetInstanceId();
                        state.renameBuffer = selectedNode->GetName();
                        state.openRenamePopup = true;
                        state.focusRenameInput = true;
                    }
                }

                // Delete / Backspace: remove selected nodes
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
                            if (node->IsSceneReferenceInternal()) continue;
                            if (node->GetParent() != nullptr) continue;
                            CollectOutlinerVisibleIds(ctx.scene, *node, state.nodeFilter, false, visibleIds);
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
                                state.pendingScrollTargetId = visibleIds[newIndex];
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
                            if (node->IsSceneReferenceInternal()) continue;
                            if (node->GetParent() != nullptr) continue;
                            CollectOutlinerVisibleIds(ctx.scene, *node, state.nodeFilter, true, visibleIds);
                        }
                        ctx.scene.SetSelection(visibleIds);
                    }
                }
            }

            if (state.openRenamePopup)
            {
                ImGui::OpenPopup("Rename Node");
                state.openRenamePopup = false;
            }

            if (Utilities::UI::BeginAnchoredPopupModal("Rename Node", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                Assets::Node* targetNode = ctx.scene.GetNodeByInstanceId(state.renameTargetId);
                if (targetNode == nullptr)
                {
                    state.renameTargetId = InvalidId;
                    state.renameBuffer.clear();
                    state.focusRenameInput = false;
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    ImGui::Text("Node:");
                    ImGui::SameLine();
                    ImGui::TextUnformatted(targetNode->GetName().c_str());
                    ImGui::Separator();

                    if (state.focusRenameInput)
                    {
                        ImGui::SetKeyboardFocusHere();
                        state.focusRenameInput = false;
                    }

                    const bool submitWithEnter =
                        ImGui::InputText("##RenameNodeInput", &state.renameBuffer, ImGuiInputTextFlags_EnterReturnsTrue);

                    bool shouldSubmit = submitWithEnter;
                    ImGui::SameLine();
                    shouldSubmit = ImGui::Button("OK") || shouldSubmit;
                    ImGui::SameLine();
                    const bool shouldCancel = ImGui::Button("Cancel");

                    if (shouldSubmit)
                    {
                        if (!state.renameBuffer.empty() && state.renameBuffer != targetNode->GetName())
                        {
                            ctx.engine.GetCommandHistory().Execute(std::make_unique<Runtime::Command::RenameNodeCommand>(
                                ctx.scene, targetNode->GetInstanceId(), state.renameBuffer));
                        }

                        state.renameTargetId = InvalidId;
                        state.renameBuffer.clear();
                        state.focusRenameInput = false;
                        ImGui::CloseCurrentPopup();
                    }
                    else if (shouldCancel)
                    {
                        state.renameTargetId = InvalidId;
                        state.renameBuffer.clear();
                        state.focusRenameInput = false;
                        ImGui::CloseCurrentPopup();
                    }
                }

                ImGui::EndPopup();
            }

            if ((ImGui::GetIO().KeyAlt) && (ImGui::IsKeyPressed(ImGuiKey_F4)))
            {
                ctx.actions.Dispatch(ctx, EEditorAction::System_RequestExit);
                ui.state = false;
            }
        }
        ImGui::End();
    }
} // namespace Editor
