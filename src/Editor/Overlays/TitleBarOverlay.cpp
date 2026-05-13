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
#include "Runtime/Editor/ProfessionalUI.hpp"
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
        ImDrawList* background = ImGui::GetBackgroundDrawList();
        const ImVec2 titleMin = viewport->Pos;
        const ImVec2 titleMax = viewport->Pos + ImVec2(viewport->Size.x, kTitleBarHeight);
        background->AddRectFilled(titleMin, titleMax, Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Background), 0.0f);
        background->AddLine(ImVec2(titleMin.x, titleMax.y - 1.0f), ImVec2(titleMax.x, titleMax.y - 1.0f),
                            Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Border));

        float menuRight = viewport->Pos.x + 210.0f;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(ImVec2(210.0f, kTitleBarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(9.0f, 6.0f));
        ImGui::Begin("EditorBrand", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);
        Runtime::UiTheme::DrawBrandMark(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), 24.0f);
        ImGui::Dummy(ImVec2(30.0f, 24.0f));
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::SetCursorPosY(8.0f);
        ImGui::TextUnformatted("gkNextEditor");
        ImGui::End();
        ImGui::PopStyleVar();

        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 210.0f, viewport->Pos.y));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - 360.0f, kTitleBarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("Menubar", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);
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
                CommandHistory& history = ctx.engine.GetCommandHistory();
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
                ImGui::MenuItem("Console", nullptr, &ui.logPanel);
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

            ImGui::EndMenuBar();
        }
        ImGui::End();

        const float dragLeftReserved = std::max(230.0f, menuRight - viewport->Pos.x + kMenuHitPadding);
        ctx.engine.ConfigureCustomTitleBarDrag(true, kTitleBarHeight, dragLeftReserved, 150.0f);

        ImGui::SetNextWindowPos(viewport->Pos + ImVec2(viewport->Size.x - 150.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(150.0f, kTitleBarHeight));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(0.0f);

        ImGui::Begin("WindowControls", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);
        ImGui::SetCursorPos(ImVec2(14.0f, 4.0f));
        if (ui.fontIcon)
        {
            ImGui::PushFont(ui.fontIcon);
        }
        if (Runtime::UiTheme::ToolbarButton(ICON_FA_WINDOW_MINIMIZE, "Minimize", false, ImVec2(38.0f, 28.0f)))
        {
            ctx.actions.Dispatch(ctx, EEditorAction::System_RequestMinimize);
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (Runtime::UiTheme::ToolbarButton(ctx.engine.IsMaximumed() ? ICON_FA_WINDOW_RESTORE : ICON_FA_WINDOW_MAXIMIZE,
                                            "Maximize", false, ImVec2(38.0f, 28.0f)))
        {
            ctx.actions.Dispatch(ctx, EEditorAction::System_ToggleMaximize);
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (Runtime::UiTheme::ToolbarButton(ICON_FA_XMARK, "Close", false, ImVec2(38.0f, 28.0f)))
        {
            ctx.actions.Dispatch(ctx, EEditorAction::System_RequestExit);
        }
        if (ui.fontIcon)
        {
            ImGui::PopFont();
        }
        ImGui::End();

        ImGui::PopStyleVar(2);

        ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - kFooterHeight));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, kFooterHeight));
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::SetNextWindowBgAlpha(1.0f);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));

        ImGui::Begin("Footer", nullptr,
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                         ImGuiWindowFlags_NoDocking);

        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(viewport->Pos.x, viewport->Pos.y + viewport->Size.y - kFooterHeight),
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y - kFooterHeight),
            Runtime::UiTheme::ColorU32(Runtime::UiTheme::EColor::Border), 1.0f);

        Runtime::UiTheme::DrawStatusDot("Ready", true);

        const float rightWidth = 430.0f;
        const float rightStart = viewport->Size.x - rightWidth;
        if (ImGui::GetCursorPosX() < rightStart)
        {
            ImGui::SameLine(rightStart);
        }
        Runtime::UiTheme::DrawBadge("Live Link", Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Success, 0.18f),
                                    Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Success));
        ImGui::SameLine(0.0f, 8.0f);
        Runtime::UiTheme::DrawBadge("Source Control",
                                    Runtime::UiTheme::Color(Runtime::UiTheme::EColor::SurfaceElevated, 0.90f),
                                    Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted));
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted), "%.0f FPS",
                           ctx.engine.GetFrameRate());
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::Blue), "Vulkan");
        ImGui::SameLine(0.0f, 10.0f);
        ImGui::TextColored(Runtime::UiTheme::Color(Runtime::UiTheme::EColor::TextMuted), "%.2f ms",
                           ctx.engine.GetSmoothDeltaSeconds() * 1000.0);

        ImGui::End();

        ImGui::PopStyleVar(3);
    }
} // namespace Editor
