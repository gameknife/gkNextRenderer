#include "Editor/EditorUi.hpp"

#include "Assets/Core/Scene.hpp"
#include "Editor/EditorActionDispatcher.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include "Editor/EditorUtils.h"

#include <spdlog/spdlog.h>

#include "Runtime/Engine.hpp"
#include "Runtime/Editor/UserInterface.hpp"

namespace Editor
{
    namespace
    {
        constexpr float kTitleBarHeight = 55.0f;
        constexpr float kFooterHeight = 40.0f;
    } // namespace

    void DrawTitleBarOverlay(EditorContext& ctx, EditorUiState& ui)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float menuUsedWidth = 0.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        // MENU
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + kTitleBarHeight, viewport->Pos.y));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - 255.0f, kTitleBarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Menubar", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);

        ImGui::GetWindowDrawList()->AddRectFilled(viewport->Pos,
                                                  viewport->Pos + ImVec2(viewport->Size.x, kTitleBarHeight),
                                                  ImGui::GetColorU32(ImGuiCol_MenuBarBg));
        ImGui::PopStyleVar();

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
                {
                    // TODO: Add file dialog for save path selection
                    const std::string filename = "saved_scene.glb";
                    const bool success = ctx.scene.Save(filename);
                    if (success)
                    {
                        SPDLOG_INFO("Scene saved successfully: {}", filename);
                    }
                    else
                    {
                        SPDLOG_ERROR("Failed to save scene: {}", filename);
                    }
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit"))
                {
                    ctx.actions.Dispatch(ctx, EEditorAction::System_RequestExit);
                }

                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                // Undo/Redo
                CommandHistory& history = ctx.engine.GetCommandHistory();
                bool canUndo = history.CanUndo();
                bool canRedo = history.CanRedo();
                
                std::string undoLabel = canUndo 
                    ? fmt::format("Undo {}", history.GetUndoDescription())
                    : "Undo";
                std::string redoLabel = canRedo 
                    ? fmt::format("Redo {}", history.GetRedoDescription())
                    : "Redo";
                
                if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, canUndo))
                {
                    history.Undo();
                }
                if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, canRedo))
                {
                    history.Redo();
                }
                
                ImGui::Separator();
                
                if (ImGui::BeginMenu("Layout"))
                {
                    ImGui::MenuItem("Reset");
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Behavior"))
                {
                    ImGui::MenuItem("Static Mode", nullptr);
                    ImGui::SameLine();
                    utils::HelpMarker("Toggle between static/linear layout and fixed/manual layout");
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Reset"))
                {
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Tools"))
            {
                ImGui::MenuItem("Style Editor", nullptr, &ui.child_style);
                ImGui::MenuItem("Demo Window", nullptr, &ui.child_demo);
                ImGui::MenuItem("Metrics", nullptr, &ui.child_metrics);
                ImGui::MenuItem("Stack Tool", nullptr, &ui.child_stack);
                ImGui::MenuItem("Color Export", nullptr, &ui.child_color);
                ImGui::MenuItem("Command History", nullptr, &ui.commandHistoryPanel);
                ImGui::MenuItem("AI Assistant", nullptr, &ui.aiPanel);
                ImGui::MenuItem("Log", nullptr, &ui.logPanel);
                ImGui::MenuItem("Material Editor", nullptr, &ui.child_mat_editor);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help"))
            {
                if (ImGui::MenuItem("Resources"))
                    ui.child_resources = true;
                if (ImGui::MenuItem("About ImStudio"))
                    ui.child_about = true;
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
            menuUsedWidth = ImGui::GetCursorPosX();
        }
        ImGui::End();

        const float dragLeftReserved = std::max(kTitleBarHeight, kTitleBarHeight + menuUsedWidth + 16.0f);
        ctx.engine.ConfigureCustomTitleBarDrag(true, kTitleBarHeight, dragLeftReserved, 200.0f);

        // LOGO
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y));
        ImGui::SetNextWindowSize(ImVec2(kTitleBarHeight, kTitleBarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Logo", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);

        ImGui::GetWindowDrawList()->AddRectFilled(viewport->Pos,
                                                  viewport->Pos + ImVec2(kTitleBarHeight, kTitleBarHeight),
                                                  ImGui::GetColorU32(ImGuiCol_MenuBarBg));
        if (ui.bigIcon)
        {
            ImGui::PushFont(ui.bigIcon);
        }
        ImGui::GetWindowDrawList()->AddText(viewport->Pos + ImVec2(10, 7), IM_COL32(240, 180, 60, 255),
                                            ICON_FA_SHEKEL_SIGN);
        if (ui.bigIcon)
        {
            ImGui::PopFont();
        }
        ImGui::End();

        // XMARK
        ImGui::SetNextWindowPos(viewport->Pos + ImVec2(viewport->Size.x - 200.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(200.0f, kTitleBarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0);

        ImGui::Begin("XMark", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);

        ImGui::GetWindowDrawList()->AddRectFilled(viewport->Pos + ImVec2(viewport->Size.x - 200.0f, 0.0f),
                                                  viewport->Pos + ImVec2(viewport->Size.x, kTitleBarHeight),
                                                  ImGui::GetColorU32(ImGuiCol_MenuBarBg));
        ImGui::SetCursorPos(ImVec2(50, 5));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        if (ImGui::Button(ICON_FA_WINDOW_MINIMIZE, ImVec2(40, 40)))
        {
            ctx.actions.Dispatch(ctx, EEditorAction::System_RequestMinimize);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_WINDOW_MAXIMIZE, ImVec2(40, 40)))
        {
            ctx.actions.Dispatch(ctx, EEditorAction::System_ToggleMaximize);
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_XMARK, ImVec2(40, 40)))
        {
            ctx.actions.Dispatch(ctx, EEditorAction::System_RequestExit);
        }
        ImGui::SameLine();
        ImGui::PopStyleColor();
        ImGui::End();

        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();

        // FOOTER
        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - kFooterHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, kFooterHeight));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));

        ImGui::Begin("Footer", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);

        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - kFooterHeight),
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y - kFooterHeight),
            IM_COL32(20, 20, 20, 255), 2);

        if (ImGui::Button(ICON_FA_HOUSE, ImVec2(60, 30)))
        {
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PEN, ImVec2(60, 30)))
        {
        }
        ImGui::SameLine();

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        ctx.ui.DrawConsoleCommandInput("##CVar", "Execute CVar...", 200.0f, false, true, "##FooterConsoleMatches");
        ImGui::PopStyleVar();
        ImGui::End();

        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
    }
} // namespace Editor
