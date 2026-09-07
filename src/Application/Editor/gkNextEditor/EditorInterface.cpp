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

    imguiIniPath_ = Utilities::FileHelper::GetWritableFilePath("editor.ini");
    io.IniFilename = imguiIniPath_.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
#if !IOS
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
#endif
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

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDocking |
                                         ImGuiWindowFlags_NoTitleBar |
                                         ImGuiWindowFlags_NoResize |
                                         ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoScrollbar |
                                         ImGuiWindowFlags_NoSavedSettings;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 0.0f));

    ImGui::Begin("TOOLBAR", nullptr, windowFlags);
    ImGui::PopStyleVar(3);

    ImDrawList* toolbarDrawList = ImGui::GetWindowDrawList();
    const ImVec2 toolbarMin = ImGui::GetWindowPos();
    const ImVec2 toolbarMax = toolbarMin + ImGui::GetWindowSize();
    toolbarDrawList->AddLine(ImVec2(toolbarMin.x, toolbarMax.y - 1.0f), toolbarMax,
                             NextUI::Theme::ColorU32(NextUI::Theme::EColor::Border, 0.18f), 1.0f);

    constexpr float kControlHeight = 28.0f;
    const float cursorY = (kToolbarSize - kControlHeight) * 0.5f;

    // ==========================================
    // Center: Play-in-Editor Capsule
    // ==========================================
    Editor::FPlaySession& play = ctx.editor->GetPlaySession();
    const bool playing = play.IsRunning();
    const bool ejected = play.State() == Editor::EPlayState::Ejected;

    // Default to the first available game if none was previously selected
    if (uiState.lastPlayedGameId.empty() && !play.Games().empty())
    {
        for (const auto& game : play.Games())
        {
            if (game.available)
            {
                uiState.lastPlayedGameId = game.id;
                break;
            }
        }
        if (uiState.lastPlayedGameId.empty())
        {
            uiState.lastPlayedGameId = play.Games().front().id;
        }
    }

    // Check selected game & rebuild availability
    const Editor::FPlayGameEntry* selected = nullptr;
    for (const Editor::FPlayGameEntry& entry : play.Games())
    {
        if (entry.id == uiState.lastPlayedGameId)
        {
            selected = &entry;
            break;
        }
    }
    const bool canRebuild = !playing && selected != nullptr && selected->canRebuild;

    // Measure centered control group widths
    const float playButtonWidth = playing ? 82.0f : 78.0f;
    const float comboWidth = 180.0f;
    const float rebuildButtonWidth = 116.0f;
    const float pauseButtonWidth = 90.0f;
    const float ejectButtonWidth = ejected ? 98.0f : 86.0f;
    constexpr float kItemGap = 6.0f;

    float centerGroupWidth = playButtonWidth;
    if (!playing)
    {
        centerGroupWidth += kItemGap + comboWidth;
        if (canRebuild)
        {
            centerGroupWidth += kItemGap + rebuildButtonWidth;
        }
    }
    else
    {
        centerGroupWidth += kItemGap + pauseButtonWidth + kItemGap + ejectButtonWidth;
    }

    const float centerX = std::floor((viewport->Size.x - centerGroupWidth) * 0.5f);

    constexpr float kPadX = 7.0f;
    constexpr float kPadY = 3.0f;
    const ImVec2 capMin(toolbarMin.x + centerX - kPadX, toolbarMin.y + cursorY - kPadY);
    const ImVec2 capMax(toolbarMin.x + centerX + centerGroupWidth + kPadX, toolbarMin.y + cursorY + kControlHeight + kPadY);
    toolbarDrawList->AddRectFilled(capMin, capMax, NextUI::Theme::ColorU32(NextUI::Theme::EColor::SurfaceElevated, 0.88f), 6.0f);

    // Position cursor at center capsule
    ImGui::SetCursorPos(ImVec2(centerX, cursorY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kItemGap, 0.0f));

    const float fontSize = ImGui::GetFontSize();
    const float framePadY = std::floor((kControlHeight - fontSize) * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, framePadY));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    // --- Play / Stop Custom Button ---
    std::string playTooltip;
    if (!play.IsAvailable())
    {
        playTooltip = "Play-in-editor is unavailable: " + play.UnavailableReason();
    }
    else if (playing)
    {
        playTooltip = "Stop '" + play.ActiveGameId() + "' and reload the open scene (F5)";
    }
    else if (uiState.lastPlayedGameId.empty())
    {
        playTooltip = "Pick a game from the list, then Play (F5)";
    }
    else
    {
        playTooltip = "Play '" + uiState.lastPlayedGameId + "' in editor (F5)";
    }

    const bool playDisabled = !play.IsAvailable() || (!playing && uiState.lastPlayedGameId.empty());
    const ImVec2 playBtnSize(playButtonWidth, kControlHeight);
    const ImVec2 playBtnMin = ImGui::GetCursorScreenPos();
    const ImVec2 playBtnMax(playBtnMin.x + playBtnSize.x, playBtnMin.y + playBtnSize.y);

    ImGui::InvisibleButton("##PlayStopHit", playBtnSize);
    const bool isPlayHovered = ImGui::IsItemHovered() && !playDisabled;
    const bool isPlayClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !playDisabled;

    if (!playTooltip.empty() && ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("%s", playTooltip.c_str());
    }

    {
        const ImU32 accentCol = playing ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Danger)
                                        : NextUI::Theme::ColorU32(NextUI::Theme::EColor::Success);
        const ImU32 bgCol = playDisabled ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Surface, 0.5f)
                                         : (isPlayHovered ? (playing ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Danger, 0.28f)
                                                                     : NextUI::Theme::ColorU32(NextUI::Theme::EColor::Success, 0.28f))
                                                          : (playing ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Danger, 0.16f)
                                                                     : NextUI::Theme::ColorU32(NextUI::Theme::EColor::Success, 0.16f)));
        const ImU32 borderCol = playDisabled ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Border, 0.4f)
                                             : (isPlayHovered ? (playing ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Danger, 0.85f)
                                                                         : NextUI::Theme::ColorU32(NextUI::Theme::EColor::Success, 0.85f))
                                                              : (playing ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::Danger, 0.55f)
                                                                         : NextUI::Theme::ColorU32(NextUI::Theme::EColor::Success, 0.55f)));

        toolbarDrawList->AddRectFilled(playBtnMin, playBtnMax, bgCol, 4.0f);
        toolbarDrawList->AddRect(playBtnMin, playBtnMax, borderCol, 4.0f, 0, 1.0f);

        const char* iconStr = playing ? ICON_FA_STOP : ICON_FA_PLAY;
        const char* textStr = playing ? "Stop" : "Play";

        const ImVec2 iconTextSize = ImGui::CalcTextSize(iconStr);
        const ImVec2 labelTextSize = ImGui::CalcTextSize(textStr);
        const float gap = 6.0f;
        const float totalContentW = iconTextSize.x + gap + labelTextSize.x;
        const float startX = playBtnMin.x + (playBtnSize.x - totalContentW) * 0.5f;
        const float textCenterY = playBtnMin.y + (playBtnSize.y - iconTextSize.y) * 0.5f;

        const ImU32 iconColor = playDisabled ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::TextDim) : accentCol;
        const ImU32 labelColor = playDisabled ? NextUI::Theme::ColorU32(NextUI::Theme::EColor::TextDim) : NextUI::Theme::ColorU32(NextUI::Theme::EColor::Text);

        toolbarDrawList->AddText(ImVec2(startX, textCenterY), iconColor, iconStr);
        toolbarDrawList->AddText(ImVec2(startX + iconTextSize.x + gap, textCenterY), labelColor, textStr);
    }

    if (isPlayClicked)
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

    // --- Game Combo or Playing Controls (Pause / Eject) ---
    if (!playing)
    {
        ImGui::SameLine();
        ImGui::BeginDisabled(!play.IsAvailable());
        ImGui::SetNextItemWidth(comboWidth);

        std::string previewText = "Select game...";
        if (!uiState.lastPlayedGameId.empty())
        {
            if (selected != nullptr && !selected->displayName.empty())
            {
                previewText = fmt::format(ICON_FA_GAMEPAD " {}", selected->displayName);
            }
            else
            {
                previewText = fmt::format(ICON_FA_GAMEPAD " {}", uiState.lastPlayedGameId);
            }
        }

        ImGui::PushStyleColor(ImGuiCol_FrameBg, NextUI::Theme::Color(NextUI::Theme::EColor::Surface, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover));
        ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.70f));

        if (ImGui::BeginCombo("##pieGame", previewText.c_str()))
        {
            for (const Editor::FPlayGameEntry& entry : play.Games())
            {
                ImGui::BeginDisabled(!entry.available);
                const std::string entryLabel = fmt::format(ICON_FA_GAMEPAD "  {}", entry.displayName);
                if (ImGui::Selectable(entryLabel.c_str(), entry.id == uiState.lastPlayedGameId))
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

            ImGui::Separator();
            const bool canCreateProject = play.CanCreateProject();
            ImGui::BeginDisabled(!canCreateProject);
            if (ImGui::Selectable(ICON_FA_PLUS "  New Game Project..."))
            {
                play.OpenNewProjectDialog();
            }
            ImGui::EndDisabled();
            if (!canCreateProject && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("%s", play.NewProjectUnavailableReason().c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();

        // Rebuild C# Action
        if (canRebuild)
        {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Button, NextUI::Theme::Color(NextUI::Theme::EColor::Surface, 0.95f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover));
            ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.70f));

            if (ImGui::Button(ICON_FA_HAMMER " Rebuild C#", ImVec2(rebuildButtonWidth, kControlHeight)))
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
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Recompile game C# code now");
            }
            ImGui::PopStyleColor(3);
        }
    }
    else
    {
        const bool paused = play.IsPaused();

        // Pause / Eject are toggles: the active state has to stay legible in a borderless theme, so
        // both the fill and the outline are pushed together with the border size they need.
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, paused ? 1.0f : 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, paused ? NextUI::Theme::Color(NextUI::Theme::EColor::Warning, 0.25f)
                                                       : NextUI::Theme::Color(NextUI::Theme::EColor::Surface, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover));
        ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Warning, 0.75f));

        const char* pauseLabel = paused ? ICON_FA_PLAY " Resume" : ICON_FA_PAUSE " Pause";
        if (ImGui::Button(pauseLabel, ImVec2(pauseButtonWidth, kControlHeight)))
        {
            play.TogglePause();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(paused ? "Resume game tick" : "Pause game tick");
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ejected ? 1.0f : 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ejected ? NextUI::Theme::Color(NextUI::Theme::EColor::Success, 0.22f)
                                                       : NextUI::Theme::Color(NextUI::Theme::EColor::Surface, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceHover));
        ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Success, 0.65f));

        const char* ejectLabel = ejected ? ICON_FA_GAMEPAD " Eject: Off" : ICON_FA_ARROW_POINTER " Eject";
        if (ImGui::Button(ejectLabel, ImVec2(ejectButtonWidth, kControlHeight)))
        {
            play.ToggleEject();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(ejected ? "Resume gameplay control (F8)"
                                      : "Eject camera and edit scene while game runs (F8)");
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }

    ImGui::PopStyleVar(3); // FramePadding, FrameRounding, FrameBorderSize
    ImGui::PopStyleVar();  // ItemSpacing

    // ==========================================
    // 3. Right Side: Actions & Utility Capsule
    // ==========================================
    constexpr float kRightItemGap = 6.0f;
    const float launchButtonWidth = 120.0f;
    const float rightGroupWidth = launchButtonWidth + (kToolbarIconWidth * 2.0f) + (kRightItemGap * 2.0f);
    const float rightStart = viewport->Size.x - 12.0f - rightGroupWidth;

    ImGui::SetCursorPos(ImVec2(rightStart, cursorY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kRightItemGap, 0.0f));

    // Detached Launch button
    if (NextUI::Foundation::Button(
            ICON_FA_UP_RIGHT_FROM_SQUARE " Detached",
            {.variant = NextUI::Foundation::EButtonVariant::Secondary,
             .size = ImVec2(launchButtonWidth, kControlHeight),
             .tooltip = "Launch current scene in standalone gkNextRenderer process"}))
    {
        LaunchRendererDetached();
    }

    // Reset Layout button
    ImGui::SameLine();
    if (NextUI::Theme::ToolbarButton(ICON_FA_ROTATE_LEFT, "Reset Layout to Default", false,
                                     ImVec2(kToolbarIconWidth, kControlHeight)))
    {
        uiState.dockResetRequested = true;
    }

    // Settings / Preferences button
    ImGui::SameLine();
    if (NextUI::Theme::ToolbarButton(ICON_FA_GEAR, "Editor Preferences", uiState.settingsPanel,
                                     ImVec2(kToolbarIconWidth, kControlHeight)))
    {
        uiState.settingsPanel = !uiState.settingsPanel;
    }

    ImGui::PopStyleVar();
    ImGui::End();

    if (const std::string createdGameId = play.DrawNewProjectDialog(); !createdGameId.empty())
    {
        uiState.lastPlayedGameId = createdGameId;
    }
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
