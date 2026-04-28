#include "Editor/EditorUi.hpp"

#include "Assets/Core/Scene.hpp"
#include "Editor/EditorActionDispatcher.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include "Editor/Core/RecentScenes.hpp"
#include "Editor/Core/EditorLayoutConstants.hpp"
#include "Editor/EditorUtils.h"

#include <spdlog/spdlog.h>
#include <SDL3/SDL_dialog.h>

#include "Runtime/Engine.hpp"
#include "Runtime/Editor/UserInterface.hpp"

namespace Editor
{
    namespace
    {
        constexpr float kMenuHitPadding = 32.0f;
    } // namespace

    void DrawTitleBarOverlay(EditorContext& ctx, EditorUiState& ui)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        float menuRight = viewport->Pos.x + kTitleBarHeight;

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
            bool fileMenuOpen = ImGui::BeginMenu("File");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (fileMenuOpen)
            {
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
                {
                    SDL_DialogFileFilter filters[] = {
                        { "Scenes", "glb;gltf;ldr;mpd" },
                        { "All Files", "*" }
                    };
                    SDL_ShowOpenFileDialog(
                        [](void* userdata, const char* const* filelist, int /*filter*/)
                        {
                            auto* editorCtx = static_cast<EditorContext*>(userdata);
                            if (filelist && filelist[0])
                            {
                                SPDLOG_INFO("Open Scene: {}", filelist[0]);
                                editorCtx->actions.Dispatch(*editorCtx, EEditorAction::IO_LoadScene,
                                                            std::string(filelist[0]));
                            }
                            else
                            {
                                SPDLOG_DEBUG("Open Scene dialog cancelled");
                            }
                        },
                        &ctx,
                        ctx.engine.GetWindow().Handle(),
                        filters, 2, nullptr, false);
                }

                if (ImGui::BeginMenu("Recent Scenes"))
                {
                    if (ui.recentScenes.empty())
                    {
                        ImGui::MenuItem("(empty)", nullptr, false, false);
                    }
                    else
                    {
                        for (size_t i = 0; i < ui.recentScenes.size(); ++i)
                        {
                            const std::string& path = ui.recentScenes[i];
                            std::string displayName = std::filesystem::path(path).filename().string();
                            if (ImGui::MenuItem(displayName.c_str(), path.c_str()))
                            {
                                ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadScene, path);
                            }
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::SetTooltip("%s", path.c_str());
                            }
                        }
                        ImGui::Separator();
                        if (ImGui::MenuItem("Clear"))
                        {
                            ui.recentScenes.clear();
                            SaveRecentScenes(ui);
                        }
                    }
                    ImGui::EndMenu();
                }

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

            bool editMenuOpen = ImGui::BeginMenu("Edit");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (editMenuOpen)
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
                    if (ImGui::MenuItem("Reset"))
                    {
                        ui.dockResetRequested = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            bool toolsMenuOpen = ImGui::BeginMenu("Tools");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (toolsMenuOpen)
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

            bool helpMenuOpen = ImGui::BeginMenu("Help");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (helpMenuOpen)
            {
                if (ImGui::MenuItem("Resources"))
                    ui.child_resources = true;
                if (ImGui::MenuItem("About ImStudio"))
                    ui.child_about = true;
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }
        ImGui::End();

        const float dragLeftReserved = std::max(kTitleBarHeight, menuRight - viewport->Pos.x + kMenuHitPadding);
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

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 6));
        ctx.ui.DrawConsoleCommandInput("##CVar", "Execute CVar...", 200.0f, false, true, "##FooterConsoleMatches");
        ImGui::PopStyleVar();
        ImGui::End();

        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
        ImGui::PopStyleVar();
    }
} // namespace Editor
