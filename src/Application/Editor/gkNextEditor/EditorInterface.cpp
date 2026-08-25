#include "EditorInterface.hpp"

#include "Engine/Utilities/Exception.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_freetype.h>

#include <array>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include "EditorUi.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "EditorActionDispatcher.hpp"
#include "EditorContext.hpp"
#include "EditorMain.h"
#include "Core/EditorLayoutConstants.hpp"
#include "Core/EditorPlaySession.hpp"
#include "Core/RecentScenes.hpp"
#include "EditorUtils.h"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Modules/NextUI/ImGuiScaling.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/NextUI/UI/UiWidgets.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/Math.hpp"

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_process.h>


namespace
{
    // Starts gkNextRenderer next to the editor executable without blocking the UI.
    // std::system() froze the editor until the renderer exited and broke on install
    // paths containing spaces; SDL_CreateProcess takes an argv array, so no quoting
    // is involved and the child runs detached.
    void LaunchRendererDetached()
    {
#if WIN32
        const char* executableName = "gkNextRenderer.exe";
#else
        const char* executableName = "gkNextRenderer";
#endif
        const std::filesystem::path executable = NextRenderer::GetExecutableDirectory() / executableName;
        std::error_code existsError;
        if (!std::filesystem::exists(executable, existsError))
        {
            SPDLOG_ERROR("Play: {} was not found next to the editor.", executable.string());
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_WARNING, "gkNextRenderer not found",
                ("Expected the renderer at:\n" + executable.string()).c_str(), nullptr);
            return;
        }

        const std::string executablePath = executable.string();
        std::vector<const char*> args;
        args.push_back(executablePath.c_str());
        if (GOption->ForceSDR)
        {
            args.push_back("--forcesdr");
        }
        args.push_back(nullptr);

        SDL_Process* process = SDL_CreateProcess(args.data(), false);
        if (process == nullptr)
        {
            SPDLOG_ERROR("Play: failed to launch {}: {}", executablePath, SDL_GetError());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Could not start gkNextRenderer",
                                     SDL_GetError(), nullptr);
            return;
        }
        // The editor does not wait for or read from the renderer; releasing the handle
        // leaves the child running on its own.
        SDL_DestroyProcess(process);
    }

    void CheckVulkanResultCallback(const VkResult err)
    {
        if (err != VK_SUCCESS)
        {
            Throw(std::runtime_error(std::string("ImGui Vulkan error (") + Vulkan::ToString(err) + ")"));
        }
    }

    const ImWchar* GetGlyphRangesFontAwesome()
    {
        static const ImWchar ranges[] = {
            ICON_MIN_FA,
            ICON_MAX_FA, // Basic Latin + Latin Supplement
            0,
        };
        return &ranges[0];
    }
} // namespace

EditorInterface::EditorInterface(class EditorGameInstance* editor) : editor_(editor) {}

EditorInterface::~EditorInterface() = default;

void EditorInterface::Config()
{
    auto& io = ImGui::GetIO();

    imguiIniPath_ = Utilities::FileHelper::GetPlatformFilePath("editor.ini");
    io.IniFilename = imguiIniPath_.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
}

void EditorInterface::Init()
{
    auto& io = ImGui::GetIO();

    // Window scaling and style.
    const auto scaleFactor = 1.0;
    // ImGui::GetStyle().ScaleAllSizes(scaleFactor);

    io.Fonts->SetFontLoader(ImGuiFreeType::GetFontLoader());
    io.Fonts->FontLoaderFlags = ImGuiFreeTypeLoaderFlags_NoHinting;
    const ImWchar* iconRange = GetGlyphRangesFontAwesome();
    ImFont* fontBigIcon = io.Fonts->AddFontFromFileTTF(
        Utilities::FileHelper::GetPlatformFilePath("assets/fonts/fa-solid-900.ttf").c_str(), 32 * scaleFactor, nullptr,
        iconRange);

    uiState_.bigIcon = fontBigIcon;
    Editor::LoadRecentScenes(uiState_);
    firstRun_ = true;
}

namespace
{
    constexpr float kToolbarSize = 38.0f;
    constexpr float kToolbarIconWidth = 34.0f;
    constexpr float kToolbarIconHeight = 30.0f;
} // namespace

Editor::EditorUiState& EditorInterface::GetRemoteUiState(std::string_view sessionId)
{
    auto [it, inserted] = remoteUiStates_.try_emplace(std::string(sessionId));
    if (inserted)
    {
        it->second.bigIcon = uiState_.bigIcon;
        it->second.recentScenes = uiState_.recentScenes;
        it->second.currentScenePath = uiState_.currentScenePath;
    }
    return it->second;
}

void EditorInterface::OnRemoteUiSessionClosed(std::string_view sessionId)
{
    remoteUiStates_.erase(std::string(sessionId));
}

ImGuiID EditorInterface::DockSpaceUI(Editor::EditorUiState& uiState)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float topOffset = kToolbarSize + Editor::kTitleBarHeight;
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + topOffset));
    ImGui::SetNextWindowSize(
        ImVec2(viewport->Size.x, viewport->Size.y - topOffset - Editor::kFooterHeight));
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0);
    ImGuiWindowFlags windowFlags = 0 | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("Master DockSpace", NULL, windowFlags);
    ImGuiID dockMain = ImGui::GetID("MyDockspace");

    if (firstRun_ || ImGui::DockBuilderGetNode(dockMain) == nullptr || uiState.dockResetRequested)
    {
        RebuildDefaultDockLayout(dockMain);
        uiState.dockResetRequested = false;
    }

    ImGui::DockSpace(dockMain, ImVec2(0, 0),
                     ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
    ImGui::PopStyleVar(3);

    return dockMain;
}

void EditorInterface::RebuildDefaultDockLayout(ImGuiID id)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace | ImGuiDockNodeFlags_NoDockingInCentralNode |
                                      ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(id, viewport->Size);

    ImGuiID dockMain = id;
    
    ImGuiID dock2 = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Right, 0.25f, nullptr, &dockMain);
    ImGuiID dock3 = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Down, 0.25f, nullptr, &dockMain);
    ImGuiID dock1 = ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.2f, nullptr, &dockMain);

    ImGui::DockBuilderDockWindow("Outliner", dock1);
    ImGui::DockBuilderDockWindow("Properties", dock2);
    ImGui::DockBuilderDockWindow("Command History", dock2);
    ImGui::DockBuilderDockWindow("Script Console", dock2);
    ImGui::DockBuilderDockWindow("Hot Reload", dock2);
    ImGui::DockBuilderDockWindow("Content Browser", dock3);
    ImGui::DockBuilderDockWindow("Sequencer", dock3);
    ImGui::DockBuilderDockWindow("Log", dock3);
    ImGui::DockBuilderDockWindow("Material Browser", dock3);
    ImGui::DockBuilderDockWindow("Texture Browser", dock3);
    ImGui::DockBuilderDockWindow("Mesh Browser", dock3);
    
    ImGui::DockBuilderFinish(id);
}

void EditorInterface::ToolbarUI(EditorContext& ctx, Editor::EditorUiState& uiState)
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + Editor::kTitleBarHeight));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, kToolbarSize));
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags windowFlags = 0 | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGui::Begin("TOOLBAR", NULL, windowFlags);
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();

    ImDrawList* toolbarDrawList = ImGui::GetWindowDrawList();
    const ImVec2 toolbarMin = ImGui::GetWindowPos();
    const ImVec2 toolbarMax = toolbarMin + ImGui::GetWindowSize();
    toolbarDrawList->AddLine(toolbarMin, ImVec2(toolbarMax.x, toolbarMin.y),
                             NextUI::Theme::ColorU32(NextUI::Theme::EColor::Border, 0.70f), 1.0f);
    toolbarDrawList->AddLine(ImVec2(toolbarMin.x, toolbarMax.y - 1.0f), toolbarMax,
                             NextUI::Theme::ColorU32(NextUI::Theme::EColor::Border, 0.92f), 1.0f);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 0.0f));
    ImGui::SetCursorPosY((kToolbarSize - kToolbarIconHeight) * 0.5f);

    Editor::FPlaySession& play = ctx.editor->GetPlaySession();
    const bool playing = play.IsRunning();

    // Play / Stop for the in-editor session. Separate from "Launch", below, which starts a
    // standalone renderer on the current scene file — a different thing that happens to share a
    // verb: this one runs a C# game inside this process, against this scene view.
    const char* playLabel = playing ? ICON_FA_STOP " Stop" : ICON_FA_PLAY " Play";
    const float playButtonWidth =
        std::ceil(ImGui::CalcTextSize(ICON_FA_PLAY " Play").x + ImGui::GetStyle().FramePadding.x * 2.0f + 16.0f);

    std::string playTooltip;
    if (!play.IsAvailable())
    {
        playTooltip = "Play-in-editor is unavailable: " + play.UnavailableReason();
    }
    else if (playing)
    {
        playTooltip = "Stop '" + play.ActiveGameId() + "' and reload the scene that was open (F5)";
    }
    else if (uiState.lastPlayedGameId.empty())
    {
        playTooltip = "Pick a game from the list to the right, then Play";
    }
    else
    {
        playTooltip = "Play '" + uiState.lastPlayedGameId + "' in this editor (F5)";
    }

    ImGui::BeginDisabled(!play.IsAvailable() || (!playing && uiState.lastPlayedGameId.empty()));
    if (NextUI::Foundation::Button(
            playLabel,
            {.variant = NextUI::Foundation::EButtonVariant::Primary,
             .tone = playing ? NextUI::Foundation::EButtonTone::Warning
                             : NextUI::Foundation::EButtonTone::Success,
             .size = ImVec2(playButtonWidth, kToolbarIconHeight),
             .tooltip = playTooltip.c_str()}))
    {
        if (playing)
        {
            play.Stop();
        }
        else
        {
            ctx.editor->StartPlaySession(uiState.lastPlayedGameId);
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!play.IsAvailable() || playing);
    ImGui::SetNextItemWidth(200.0f);
    const char* preview = uiState.lastPlayedGameId.empty() ? "Select a game..." : uiState.lastPlayedGameId.c_str();
    if (ImGui::BeginCombo("##pieGame", preview))
    {
        for (const Editor::FPlayGameEntry& entry : play.Games())
        {
            ImGui::BeginDisabled(!entry.available);
            if (ImGui::Selectable(entry.displayName.c_str(), entry.id == uiState.lastPlayedGameId))
            {
                uiState.lastPlayedGameId = entry.id;
            }
            ImGui::EndDisabled();
            if (!entry.available)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", entry.unavailableReason.c_str());
            }
        }
        if (play.Games().empty())
        {
            ImGui::TextDisabled("no games under assets/configs/games");
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    // Rebuilding is a stopped-only action on purpose: it publishes over the assembly the next Play
    // will load, and swapping it under a running game is the hot-reload path, not this one.
    const Editor::FPlayGameEntry* selected = nullptr;
    for (const Editor::FPlayGameEntry& entry : play.Games())
    {
        if (entry.id == uiState.lastPlayedGameId)
        {
            selected = &entry;
            break;
        }
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(playing || selected == nullptr || !selected->canRebuild);
    if (NextUI::Foundation::Button(
            ICON_FA_HAMMER " Rebuild C#",
            {.size = ImVec2(0.0f, kToolbarIconHeight),
             .tooltip = "Recompile this game's C# now; the next Play uses it"}))
    {
        std::string error;
        if (play.Rebuild(uiState.lastPlayedGameId, error))
        {
            SPDLOG_INFO("[pie] rebuilt {}", uiState.lastPlayedGameId);
        }
        else
        {
            SPDLOG_ERROR("[pie] rebuild failed: {}", error);
        }
    }
    ImGui::EndDisabled();

    if (playing)
    {
        ImGui::SameLine();
        const bool ejected = play.State() == Editor::EPlayState::Ejected;
        if (NextUI::Foundation::Button(
                ejected ? ICON_FA_GAMEPAD " Resume" : ICON_FA_ARROW_POINTER " Eject",
                {.tone = ejected ? NextUI::Foundation::EButtonTone::Success
                                 : NextUI::Foundation::EButtonTone::Default,
                 .size = ImVec2(0.0f, kToolbarIconHeight),
                 .tooltip = ejected
                     ? "Give input and the camera back to the game (F8)"
                     : "Take input and the camera back from the game so you can inspect and edit "
                       "its scene while it keeps running (F8)"}))
        {
            play.ToggleEject();
        }
        if (ejected)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.25f, 1.0f), ICON_FA_CIRCLE_INFO " ejected");
        }
    }

    ImGui::SameLine();
    if (NextUI::Foundation::Button(
            ICON_FA_UP_RIGHT_FROM_SQUARE " Launch",
            {.size = ImVec2(0.0f, kToolbarIconHeight),
             .tooltip = "Run the current scene in a separate gkNextRenderer process"}))
    {
        LaunchRendererDetached();
    }

    const float rightStart = viewport->Size.x - kToolbarIconWidth - 12.0f;
    if (ImGui::GetCursorPosX() < rightStart)
    {
        ImGui::SameLine(rightStart);
    }
    if (NextUI::Theme::ToolbarButton(ICON_FA_GEAR, "Editor Settings", uiState.settingsPanel,
                                     ImVec2(kToolbarIconWidth, kToolbarIconHeight)))
    {
        uiState.settingsPanel = !uiState.settingsPanel;
    }

    ImGui::PopStyleVar();
    ImGui::End();
}

void EditorInterface::Render()
{
    Render(uiState_);
}

void EditorInterface::Render(const NextGameInstanceBase::FGameUiFrameContext& context)
{
    if (context.surfaceKind == NextGameInstanceBase::FGameUiFrameContext::ESurfaceKind::RemoteView)
    {
        Render(GetRemoteUiState(context.sessionId));
        return;
    }

    Render(uiState_);
}

void EditorInterface::Render(Editor::EditorUiState& uiState)
{
    NextUI::IUserInterface* ui = editor_->GetEngine().GetUserInterface();
    if (ui == nullptr)
    {
        return;
    }

    EditorContext ctx{editor_->GetEngine(), editor_->GetEngine().GetScene(), *ui, editor_->Actions(),
                      editor_->GetEditorSettings(), &editor_->GetGizmoController(), editor_};
    void* previousUserData = ImGui::GetIO().UserData;
    ImGui::GetIO().UserData = &ctx;

    uiState.selected_obj_id = ctx.scene.GetSelectedId();
    
    // Global keyboard shortcuts are handled by NextEngine.

    ImGuiID id = DockSpaceUI(uiState);
    ToolbarUI(ctx, uiState);

    Editor::DrawTitleBarOverlay(ctx, uiState);

    // default left
    if (uiState.sidebar) Editor::DrawOutlinerPanel(ctx, uiState);
    
    // default right
    if (uiState.commandHistoryPanel) Editor::DrawCommandHistoryPanel(ctx, uiState);
    if (uiState.hotReloadPanel) Editor::DrawHotReloadPanel(ctx, uiState);
    if (uiState.scriptConsolePanel) Editor::DrawScriptConsolePanel(ctx, uiState);
    if (uiState.properties) Editor::DrawPropertiesPanel(ctx, uiState);
    
    // default bottom
    if (uiState.logPanel) Editor::DrawConsoleLogPanel(ctx, uiState);
    if (uiState.sequencerPanel) Editor::DrawSequencerPanel(ctx, uiState);
    if (uiState.contentBrowser || uiState.materialBrowser || uiState.textureBrowser || uiState.meshBrowser) Editor::DrawContentBrowserPanel(ctx, uiState);
    
    Editor::DrawCameraViewPanel(ctx, uiState);
    if (uiState.child_style) utils::ShowStyleEditorWindow(&uiState.child_style);
    if (uiState.child_demo) ImGui::ShowDemoWindow(&uiState.child_demo);
    if (uiState.child_metrics) ImGui::ShowMetricsWindow(&uiState.child_metrics);
    if (uiState.child_debug_log) ImGui::ShowDebugLogWindow(&uiState.child_debug_log);
    if (uiState.child_stack) ImGui::ShowIDStackToolWindow(&uiState.child_stack);
    if (uiState.child_color) utils::ShowColorExportWindow(&uiState.child_color);
    if (uiState.child_resources) utils::ShowResourcesWindow(&uiState.child_resources);
    if (uiState.child_about) utils::ShowAboutWindow(&uiState.child_about);
    if (uiState.ed_material) Editor::DrawMaterialEditorPanel(ctx, uiState);

    bool activeCameraViewOpen = true;
    switch (uiState.activeViewport)
    {
    case Editor::EEditorViewportId::CameraView0:
        activeCameraViewOpen = uiState.cameraViews[0].open;
        break;
    case Editor::EEditorViewportId::CameraView1:
        activeCameraViewOpen = uiState.cameraViews[1].open;
        break;
    case Editor::EEditorViewportId::CameraView2:
        activeCameraViewOpen = uiState.cameraViews[2].open;
        break;
    default:
        break;
    }
    if (!activeCameraViewOpen)
    {
        uiState.activeViewport = Editor::EEditorViewportId::Scene;
    }

    // The renderer output rect is the dockspace central node.
    uiState.viewportOnMainViewport = true;
    uiState.viewportContentPos = ImVec2(0.0f, 0.0f);
    uiState.viewportContentSize = ImVec2(0.0f, 0.0f);
    uiState.viewportHovered = false;
    uiState.viewportFocused = uiState.activeViewport == Editor::EEditorViewportId::Scene;

    if (ImGuiDockNode* node = ImGui::DockBuilderGetCentralNode(id))
    {
        uiState.viewportContentPos = node->Pos;
        uiState.viewportContentSize = node->Size;

        ImGuiViewport* mainViewport = ImGui::GetMainViewport();
        if (node->HostWindow && node->HostWindow->Viewport && node->HostWindow->Viewport != mainViewport)
        {
            uiState.viewportOnMainViewport = false;
        }

        if (uiState.viewportOnMainViewport && node->Size.x >= 1.0f && node->Size.y >= 1.0f)
        {
            const NextUI::Scaling::FViewportRect framebufferViewport =
                NextUI::Scaling::ImGuiToMainFramebufferViewport(node->Pos, node->Size);
            const int32_t outputX = Utilities::Math::floorToInt(framebufferViewport.Position.x);
            const int32_t outputY = Utilities::Math::floorToInt(framebufferViewport.Position.y);
            const uint32_t outputWidth = Utilities::Math::ceilToInt(framebufferViewport.Size.x);
            const uint32_t outputHeight = Utilities::Math::ceilToInt(framebufferViewport.Size.y);
            // Hand the rect to the renderer rather than poking the swapchain's output viewport
            // directly: the renderer also sizes the render extent and the RT bank to it, so the
            // scene costs what the panel shows instead of what the whole window would.
            const VkRect2D sceneViewport{{outputX, outputY}, {outputWidth, outputHeight}};
            const bool outputViewportChanged =
                sceneViewportRect_.offset.x != sceneViewport.offset.x ||
                sceneViewportRect_.offset.y != sceneViewport.offset.y ||
                sceneViewportRect_.extent.width != sceneViewport.extent.width ||
                sceneViewportRect_.extent.height != sceneViewport.extent.height;
            sceneViewportRect_ = sceneViewport;
            editor_->GetEngine().GetRenderer().SetSceneViewportRect(sceneViewport);
            if (outputViewportChanged)
            {
                editor_->GetEngine().ResetProgressiveRenderingAccumulation();
                editor_->GetEngine().GetRenderer().PrimaryView().InvalidateTemporalHistory(
                    Vulkan::EHistoryInvalidationReason::ExtentChanged);
            }

            const ImGuiIO& io = ImGui::GetIO();
            const ImVec2 mousePos = io.MousePos;
            const bool mouseInSceneViewport =
                mousePos.x >= node->Pos.x && mousePos.y >= node->Pos.y &&
                mousePos.x < node->Pos.x + node->Size.x && mousePos.y < node->Pos.y + node->Size.y;
            bool mouseInCameraView = false;
            for (const auto& cameraView : uiState.cameraViews)
            {
                mouseInCameraView = mouseInCameraView ||
                    (cameraView.open &&
                     mousePos.x >= cameraView.contentPos.x && mousePos.y >= cameraView.contentPos.y &&
                     mousePos.x < cameraView.contentPos.x + cameraView.contentSize.x &&
                     mousePos.y < cameraView.contentPos.y + cameraView.contentSize.y);
            }
            uiState.viewportHovered = mouseInSceneViewport;
            const bool sceneViewportClicked = mouseInSceneViewport && !mouseInCameraView && !io.WantCaptureMouse &&
                (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                 ImGui::IsMouseClicked(ImGuiMouseButton_Middle));
            if (sceneViewportClicked)
            {
                ImGui::SetWindowFocus(nullptr);
                uiState.activeViewport = Editor::EEditorViewportId::Scene;
            }
            uiState.viewportFocused = uiState.activeViewport == Editor::EEditorViewportId::Scene;

            if (uiState.activeViewport == Editor::EEditorViewportId::Scene)
            {
                editor_->DrawGizmo(glm::vec2(node->Pos.x, node->Pos.y), glm::vec2(node->Size.x, node->Size.y));
            }
        }
    }
    
    if (uiState.viewport)
        Editor::DrawViewportOverlay(ctx, uiState);
    if (uiState.settingsPanel)
        Editor::DrawSettingsPanel(ctx, uiState);

    firstRun_ = false;
    ImGui::GetIO().UserData = previousUserData;
}
