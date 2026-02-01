#include "Editor/EditorUi.hpp"

#include "Editor/EditorActionDispatcher.hpp"
#include "Editor/EditorUtils.h"

#include "Assets/Node.h"
#include "Assets/Scene.hpp"
#include "Runtime/Components/RenderComponent.h"

#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

namespace Editor
{
    namespace
    {
        void DrawNode(EditorContext& ctx, Assets::Node& node)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            const bool selected = ctx.scene.GetSelectedId() == node.GetInstanceId();
            ImGuiTreeNodeFlags flag = ImGuiTreeNodeFlags_FramePadding |
                ImGuiTreeNodeFlags_OpenOnArrow |      // Only expand on arrow click
                ImGuiTreeNodeFlags_SpanAvailWidth |   // Make the whole row clickable
                (selected ? ImGuiTreeNodeFlags_Selected : 0) |
                (node.Children().empty() ? ImGuiTreeNodeFlags_Leaf : 0);

            ImGui::PushID(static_cast<int>(node.GetInstanceId()));
            ImGui::PushStyleColor(ImGuiCol_Text, selected ? ActiveColor : ImGui::GetColorU32(ImGuiCol_Text));
            auto render = node.GetComponent<Runtime::RenderComponent>();
            const int modelId = render ? render->GetModelId() : -1;

            const std::string label =
                (modelId == -1 ? ICON_FA_CIRCLE_NOTCH : ICON_FA_CUBE) + std::string(" ") + node.GetName();
            const bool opened = ImGui::TreeNodeEx(label.c_str(), flag);

            ImGui::PopStyleColor();

            // Single click to select
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
            {
                ctx.scene.SetSelectedId(node.GetInstanceId());
            }

            // Double-click to focus camera on the node
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                ctx.scene.SetSelectedId(node.GetInstanceId());
                ctx.actions.Dispatch(ctx, EEditorAction::Camera_FocusSelected,
                                     std::to_string(node.GetInstanceId()));
            }

            if (opened)
            {
                for (auto& child : node.Children())
                {
                    DrawNode(ctx, *child);
                }
                ImGui::TreePop();
            }

            ImGui::PopID();
        }
    } // namespace

    void DrawOutlinerPanel(EditorContext& ctx, EditorUiState& ui)
    {
        ImGui::Begin("Outliner", nullptr);
        {
            ImGui::TextDisabled("NOTE");
            ImGui::SameLine();
            utils::HelpMarker("ALL SCENE NODES\n"
                              "limited to 1000 nodes\n"
                              "select and view node properties\n");
            ImGui::Separator();

            ImGui::Text("Nodes");
            ImGui::Separator();

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

                    DrawNode(ctx, *node);

                    if (limit-- <= 0)
                    {
                        break;
                    }
                }
                ImGui::EndTable();
            }

            ImGui::EndChild();

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
