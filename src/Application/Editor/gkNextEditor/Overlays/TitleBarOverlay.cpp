#include "EditorUi.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "EditorActionDispatcher.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include "Core/RecentScenes.hpp"
#include "Core/EditorLayoutConstants.hpp"
#include "EditorUtils.h"

#include <spdlog/spdlog.h>
#include <SDL3/SDL_dialog.h>

#include "Engine/Runtime/Engine.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Modules/DevTools/UiDevPanels.hpp"

namespace Editor
{
    namespace
    {
        constexpr const char* kWindowTitle = "gkNextEditor";
    } // namespace

    void DrawTitleBarOverlay(EditorContext& ctx, EditorUiState& ui)
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();

        NextUI::Theme::FAppTitleBarConfig config{};
        config.BrandWindowId = "EditorBrand";
        config.MenuWindowId = "EditorMenuBar";
        config.RightWindowId = "EditorWindowControls";
        config.AppName = kWindowTitle;
        config.Height = kTitleBarHeight;
        config.IsMaximized = ctx.engine.IsMaximized();
        config.DrawMenuBar = [&]() -> float
        {
            float menuRight = ImGui::GetCursorScreenPos().x;
            bool fileMenuOpen = ImGui::BeginMenu("File");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (fileMenuOpen)
            {
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O"))
                {
                    SDL_DialogFileFilter filters[] = {
                        {"Scenes", "glb;gltf;ldr;mpd"},
                        {"All Files", "*"},
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
                        for (const std::string& path : ui.recentScenes)
                        {
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

                if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                {
                    const std::string filename = ui.currentScenePath.empty() ? "saved_scene.glb" : ui.currentScenePath;
                    const bool success = ctx.scene.Save(filename);
                    if (success)
                    {
                        ui.currentScenePath = filename;
                        SPDLOG_INFO("Scene saved successfully: {}", filename);
                    }
                    else
                    {
                        SPDLOG_ERROR("Failed to save scene: {}", filename);
                    }
                }
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
                {
                    const std::string filename = "saved_scene.glb";
                    const bool success = ctx.scene.Save(filename);
                    if (success)
                    {
                        ui.currentScenePath = filename;
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
                Runtime::Command::CommandHistory& history = ctx.engine.GetCommandHistory();
                const bool canUndo = history.CanUndo();
                const bool canRedo = history.CanRedo();

                const std::string undoLabel = canUndo ? fmt::format("Undo {}", history.GetUndoDescription()) : "Undo";
                const std::string redoLabel = canRedo ? fmt::format("Redo {}", history.GetRedoDescription()) : "Redo";

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
                    if (ImGui::MenuItem("Reset Dock Layout"))
                    {
                        ui.dockResetRequested = true;
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            bool viewMenuOpen = ImGui::BeginMenu("View");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (viewMenuOpen)
            {
                static int viewportMode = 0;
                static bool showGrid = true;
                static bool showBounds = false;
                static bool showIcons = true;
                static bool gizmoTranslate = true;
                static bool gizmoRotate = false;
                static bool gizmoScale = false;
                static bool snapEnabled = true;

                if (ImGui::BeginMenu("Viewport Display Mode"))
                {
                    if (ImGui::MenuItem("Lit", nullptr, viewportMode == 0))
                    {
                        viewportMode = 0;
                    }
                    if (ImGui::MenuItem("Lighting Only", nullptr, viewportMode == 1))
                    {
                        viewportMode = 1;
                    }
                    if (ImGui::MenuItem("Wireframe", nullptr, viewportMode == 2))
                    {
                        viewportMode = 2;
                    }
                    if (ImGui::MenuItem("Unlit", nullptr, viewportMode == 3))
                    {
                        viewportMode = 3;
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                ImGui::MenuItem("Show Grid", nullptr, &showGrid);
                ImGui::MenuItem("Show Bounds", nullptr, &showBounds);
                ImGui::MenuItem("Show Icons", nullptr, &showIcons);

                if (ImGui::BeginMenu("Gizmo"))
                {
                    if (ImGui::MenuItem("Translate", "W", gizmoTranslate))
                    {
                        gizmoTranslate = true;
                        gizmoRotate = false;
                        gizmoScale = false;
                    }
                    if (ImGui::MenuItem("Rotate", "E", gizmoRotate))
                    {
                        gizmoTranslate = false;
                        gizmoRotate = true;
                        gizmoScale = false;
                    }
                    if (ImGui::MenuItem("Scale", "R", gizmoScale))
                    {
                        gizmoTranslate = false;
                        gizmoRotate = false;
                        gizmoScale = true;
                    }
                    ImGui::Separator();
                    ImGui::MenuItem("Enable Snap", nullptr, &snapEnabled);
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
                ImGui::MenuItem("Hot Reload", nullptr, &ui.hotReloadPanel);
                ImGui::MenuItem("AI Assistant", nullptr, &ui.aiPanel);
                ImGui::MenuItem("Log", nullptr, &ui.logPanel);
                ImGui::MenuItem("Material Editor", nullptr, &ui.child_mat_editor);
                ImGui::EndMenu();
            }

            bool buildMenuOpen = ImGui::BeginMenu("Build");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (buildMenuOpen)
            {
                ImGui::MenuItem("Cook Assets", nullptr, false, false);
                ImGui::MenuItem("Package Project", nullptr, false, false);
                ImGui::MenuItem("Launch Renderer", nullptr, false, false);
                ImGui::EndMenu();
            }

            bool windowsMenuOpen = ImGui::BeginMenu("Windows");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (windowsMenuOpen)
            {
                ImGui::MenuItem("Outliner", nullptr, &ui.sidebar);
                ImGui::MenuItem("Properties", nullptr, &ui.properties);
                ImGui::MenuItem("Content Browser", nullptr, &ui.contentBrowser);
                const bool consoleOpen = DevTools::FUiDevPanels::Get().IsConsoleOpen();
                if (ImGui::MenuItem("Console", nullptr, consoleOpen))
                {
                    DevTools::FUiDevPanels::Get().ToggleConsole();
                }
                ImGui::MenuItem("Material Editor", nullptr, &ui.child_mat_editor);

                ImGui::Separator();
                ImGui::MenuItem("Material Browser", nullptr, &ui.materialBrowser);
                ImGui::MenuItem("Texture Browser", nullptr, &ui.textureBrowser);
                ImGui::MenuItem("Mesh Browser", nullptr, &ui.meshBrowser);
                ImGui::MenuItem("AI Assistant", nullptr, &ui.aiPanel);
                ImGui::MenuItem("Command History", nullptr, &ui.commandHistoryPanel);
                ImGui::MenuItem("Hot Reload", nullptr, &ui.hotReloadPanel);
                ImGui::EndMenu();
            }

            bool helpMenuOpen = ImGui::BeginMenu("Help");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (helpMenuOpen)
            {
                if (ImGui::MenuItem("Resources"))
                    ui.child_resources = true;
                if (ImGui::MenuItem("About gkNextEditor"))
                    ui.child_about = true;
                ImGui::EndMenu();
            }

            return menuRight;
        };
        config.OnMinimize = [&]()
        {
            ctx.actions.Dispatch(ctx, EEditorAction::System_RequestMinimize);
        };
        config.OnToggleMaximize = [&]()
        {
            ctx.actions.Dispatch(ctx, EEditorAction::System_ToggleMaximize);
        };
        config.OnClose = [&]()
        {
            ctx.actions.Dispatch(ctx, EEditorAction::System_RequestExit);
        };
        NextUI::Theme::DrawAppTitleBar(ctx.engine, config);

        NextUI::Theme::DrawStandardBottomBar(ctx.engine, "Footer", kFooterHeight);
    }
} // namespace Editor
