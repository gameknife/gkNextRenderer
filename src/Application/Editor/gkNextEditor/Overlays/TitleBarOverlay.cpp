#include "EditorUi.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "EditorActionDispatcher.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include "Core/RecentScenes.hpp"
#include "Core/EditorLayoutConstants.hpp"
#include "Core/SceneSavePolicy.hpp"
#include "EditorUtils.h"

#include <spdlog/spdlog.h>
#include <SDL3/SDL_dialog.h>

#include "Engine/Runtime/Engine.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Modules/DevTools/UiDevPanels.hpp"

namespace Editor
{
    namespace
    {
        constexpr const char* kWindowTitle = "gkNextEditor";
        constexpr SDL_DialogFileFilter kSceneOpenFilters[] = {
            {"Scenes", "glb;gltf;ldr;mpd"},
            {"All Files", "*"},
        };
        constexpr SDL_DialogFileFilter kSceneSaveFilters[] = {
            {"GLB Scene", "glb"},
            {"All Files", "*"},
        };

        struct FSaveSceneDialogContext
        {
            EditorUiState* Ui = nullptr;
            std::string DefaultLocation;
        };

        struct FOpenSceneDialogContext
        {
            EditorUiState* Ui = nullptr;
        };

        void SaveSceneToPath(EditorContext& ctx, EditorUiState& ui, const std::string& filename)
        {
            if (!IsGlbScenePath(filename))
            {
                SPDLOG_ERROR("Scene save target must be a .glb file: {}", filename);
                return;
            }

            const std::string savePath = ResolveSceneFilesystemPath(filename).string();
            const bool success = ctx.scene.Save(savePath);
            if (success)
            {
                ui.currentScenePath = filename;
                PushRecentScene(ui, filename);
                SPDLOG_INFO("Scene saved successfully: {}", savePath);
            }
            else
            {
                SPDLOG_ERROR("Failed to save scene: {}", savePath);
            }
        }
    } // namespace

    void DrawTitleBarOverlay(EditorContext& ctx, EditorUiState& ui)
    {
        std::string pendingSaveScenePath;
        std::string pendingOpenScenePath;
        {
            std::scoped_lock lock(ui.sceneDialogMutex);
            pendingOpenScenePath = std::move(ui.pendingOpenScenePath);
            ui.pendingOpenScenePath.clear();
            pendingSaveScenePath = std::move(ui.pendingSaveScenePath);
            ui.pendingSaveScenePath.clear();
        }
        if (!pendingOpenScenePath.empty())
        {
            ctx.actions.Dispatch(ctx, EEditorAction::IO_LoadScene, pendingOpenScenePath);
        }
        if (!pendingSaveScenePath.empty())
        {
            SaveSceneToPath(ctx, ui, pendingSaveScenePath);
        }

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
                    auto* dialogContext = new FOpenSceneDialogContext{&ui};
                    SDL_ShowOpenFileDialog(
                        [](void* userdata, const char* const* filelist, int /*filter*/)
                        {
                            std::unique_ptr<FOpenSceneDialogContext> dialogContext(
                                static_cast<FOpenSceneDialogContext*>(userdata));
                            if (!dialogContext || !dialogContext->Ui)
                            {
                                return;
                            }

                            if (filelist && filelist[0])
                            {
                                SPDLOG_INFO("Open Scene: {}", filelist[0]);
                                std::scoped_lock lock(dialogContext->Ui->sceneDialogMutex);
                                dialogContext->Ui->pendingOpenScenePath = filelist[0];
                            }
                            else
                            {
                                SPDLOG_DEBUG("Open Scene dialog cancelled");
                            }
                        },
                        dialogContext,
                        ctx.engine.GetWindow().Handle(),
                        kSceneOpenFilters, 2, nullptr, false);
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

                const bool canSaveCurrentScene = CanOverwriteCurrentScene(ui.currentScenePath);
                if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, canSaveCurrentScene))
                {
                    SaveSceneToPath(ctx, ui, ui.currentScenePath);
                }
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
                {
                    auto* dialogContext = new FSaveSceneDialogContext{&ui, DefaultSceneSaveDirectory().string()};
                    SDL_ShowSaveFileDialog(
                        [](void* userdata, const char* const* filelist, int /*filter*/)
                        {
                            std::unique_ptr<FSaveSceneDialogContext> dialogContext(
                                static_cast<FSaveSceneDialogContext*>(userdata));
                            if (!dialogContext || !dialogContext->Ui)
                            {
                                return;
                            }

                            if (filelist && filelist[0])
                            {
                                std::string filename = NormalizeSaveAsScenePath(filelist[0]);
                                std::scoped_lock lock(dialogContext->Ui->sceneDialogMutex);
                                dialogContext->Ui->pendingSaveScenePath = std::move(filename);
                            }
                            else
                            {
                                SPDLOG_DEBUG("Save Scene As dialog cancelled");
                            }
                        },
                        dialogContext,
                        ctx.engine.GetWindow().Handle(),
                        kSceneSaveFilters, 2, dialogContext->DefaultLocation.c_str());
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
                ImGui::Separator();
                ImGui::MenuItem("Preferences...", "Ctrl+,", &ui.settingsPanel);
                ImGui::EndMenu();
            }

            bool viewMenuOpen = ImGui::BeginMenu("View");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (viewMenuOpen)
            {
                Runtime::Config::ShowFlags& showFlags = ctx.engine.GetShowFlags();
                const Runtime::GraphicsDebugPanel::EViewMode viewportMode =
                    Runtime::GraphicsDebugPanel::ResolveViewMode(showFlags);

                if (ImGui::BeginMenu("Viewport Display Mode"))
                {
                    if (ImGui::MenuItem("Lit", nullptr, viewportMode == Runtime::GraphicsDebugPanel::EViewMode::Lit))
                    {
                        Runtime::GraphicsDebugPanel::ApplyViewMode(
                            showFlags, Runtime::GraphicsDebugPanel::EViewMode::Lit);
                    }
                    if (ImGui::MenuItem("Lighting Debug", nullptr,
                                        viewportMode == Runtime::GraphicsDebugPanel::EViewMode::Lighting))
                    {
                        Runtime::GraphicsDebugPanel::ApplyViewMode(
                            showFlags, Runtime::GraphicsDebugPanel::EViewMode::Lighting);
                    }
                    if (ImGui::MenuItem("Wireframe", nullptr,
                                        viewportMode == Runtime::GraphicsDebugPanel::EViewMode::Wireframe))
                    {
                        Runtime::GraphicsDebugPanel::ApplyViewMode(
                            showFlags, Runtime::GraphicsDebugPanel::EViewMode::Wireframe);
                    }
                    if (ImGui::MenuItem("Visual Debug", nullptr,
                                        viewportMode == Runtime::GraphicsDebugPanel::EViewMode::VisualDebug))
                    {
                        Runtime::GraphicsDebugPanel::ApplyViewMode(
                            showFlags, Runtime::GraphicsDebugPanel::EViewMode::VisualDebug);
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                ImGui::MenuItem("Show Grid", nullptr, &showFlags.ShowGrid);
                ImGui::MenuItem("Show Bounds", nullptr, &showFlags.DebugDraw_BoundingBox);
                ImGui::MenuItem("Wireframe", nullptr, &showFlags.ShowWireframe);
                ImGui::MenuItem("Gizmo Snap", nullptr, &ctx.settings.gizmoSnap);
                ImGui::EndMenu();
            }

            bool toolsMenuOpen = ImGui::BeginMenu("Tools");
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
            if (toolsMenuOpen)
            {
                if (ImGui::BeginMenu("ImGui Debugger"))
                {
                    if (ImGui::MenuItem("Item Picker", "Ctrl+Shift+C"))
                    {
                        ImGui::DebugStartItemPicker();
                    }
                    ImGui::MenuItem("Stack Tool", nullptr, &ui.child_stack);
                    ImGui::MenuItem("Metrics / Debugger", nullptr, &ui.child_metrics);
                    ImGui::MenuItem("Debug Log", nullptr, &ui.child_debug_log);
                    ImGui::MenuItem("Demo Window", nullptr, &ui.child_demo);
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                ImGui::MenuItem("Style Editor", nullptr, &ui.child_style);
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
                ImGui::Separator();
                ImGui::MenuItem("Camera View 1", nullptr, &ui.cameraViews[0].open);
                ImGui::MenuItem("Camera View 2", nullptr, &ui.cameraViews[1].open);
                ImGui::MenuItem("Camera View 3", nullptr, &ui.cameraViews[2].open);
                ui.cameraViewPanel = ui.cameraViews[0].open;
                ImGui::Separator();
                ImGui::MenuItem("AI Assistant", nullptr, &ui.aiPanel);
                ImGui::MenuItem("Command History", nullptr, &ui.commandHistoryPanel);
                ImGui::MenuItem("Hot Reload", nullptr, &ui.hotReloadPanel);
                ImGui::MenuItem("Settings", nullptr, &ui.settingsPanel);
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
