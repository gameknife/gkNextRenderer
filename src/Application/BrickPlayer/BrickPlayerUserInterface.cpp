#include "BrickPlayerUserInterface.hpp"
#include "BrickPlayerGameInstance.hpp"
#include "Utilities/ImGui.hpp"
#include <imgui_internal.h>

namespace
{
    constexpr float titleBarHeight = 40.0f;
    constexpr float titleBarControlWidth = titleBarHeight * 3;
    constexpr float titleBarActionWidth = titleBarHeight * 12;
}

BrickPlayerUserInterface::BrickPlayerUserInterface(BrickPlayerGameInstance* gameInstance)
    : gameInstance_(gameInstance)
{
}

void BrickPlayerUserInterface::ApplyStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    // No rounding
    style.WindowRounding = 0.0f;
    style.FrameRounding = 0.0f;
    style.PopupRounding = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding = 0.0f;
    style.TabRounding = 0.0f;

    // Colors
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.6f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.2f, 0.2f, 0.2f, 0.4f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.3f, 0.55f, 0.85f, 0.5f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.3f, 0.55f, 0.85f, 0.8f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.1f, 0.1f, 0.1f, 0.3f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.2f, 0.2f, 0.2f, 0.4f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.3f, 0.3f, 0.3f, 0.5f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.95f, 0.95f, 0.95f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.3f, 0.55f, 0.85f, 0.8f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.4f, 0.65f, 0.95f, 1.0f);
}

void BrickPlayerUserInterface::Render()
{
    gameInstance_->SetMouseCapturedByUI(ImGui::GetIO().WantCaptureMouse);

    RenderTitleBar();

    if (!gameInstance_->IsSceneLoaded())
    {
        RenderWelcomeScreen();
        return;
    }

    if (gameInstance_->IsFreeBuildMode())
        RenderFreeBuildToolbar();
    else
        RenderTimeline();
    gameInstance_->DrawSnapConfirmation();
    gameInstance_->DrawSnapDebug();
}

void BrickPlayerUserInterface::RenderTitleBar()
{
    ImVec2 windowSize = ImGui::GetMainViewport()->Size;
    float titleBarLeftReservedWidth = 0.0f;
    float titleBarRightReservedWidth = titleBarControlWidth;

    // Title bar background
    auto bgColor = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    bgColor.w = 0.8f;
    ImGui::GetBackgroundDrawList()->AddRectFilled(
        ImVec2(0, 0), ImVec2(windowSize.x, titleBarHeight),
        ImGui::ColorConvertFloat4ToU32(bgColor));

    // Centered title text
    const char* titleText = "BrickPlayer";
    auto textSize = ImGui::CalcTextSize(titleText);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2((windowSize.x - textSize.x) * 0.5f, (titleBarHeight - textSize.y) * 0.5f),
        IM_COL32(255, 255, 255, 255), titleText);

    // ---- Right side: window control buttons ----
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::SetNextWindowPos(ImVec2(windowSize.x - titleBarControlWidth, 0));
    ImGui::SetNextWindowSize(ImVec2(titleBarControlWidth, titleBarHeight));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::Begin("##TitleBarRight", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBackground);
    titleBarRightReservedWidth = ImGui::GetWindowSize().x;

    if (ImGui::Button(ICON_FA_MINUS, ImVec2(titleBarHeight, titleBarHeight)))
    {
        gameInstance_->GetEngine().RequestMinimize();
    }
    ImGui::SameLine();
    if (ImGui::Button(gameInstance_->GetEngine().IsMaximumed() ? ICON_FA_WINDOW_RESTORE : ICON_FA_SQUARE,
                      ImVec2(titleBarHeight, titleBarHeight)))
    {
        gameInstance_->GetEngine().ToggleMaximize();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK, ImVec2(titleBarHeight, titleBarHeight)))
    {
        gameInstance_->GetEngine().RequestClose();
    }
    ImGui::End();

    // ---- Left side: action buttons ----
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(titleBarActionWidth, titleBarHeight));
    ImGui::SetNextWindowBgAlpha(0.0f);

    ImGui::Begin("##TitleBarLeft", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBackground);

    if (ImGui::Button(ICON_FA_FOLDER_OPEN, ImVec2(titleBarHeight, titleBarHeight)))
    {
        gameInstance_->OpenFileDialog();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open File");

    if (gameInstance_->IsSceneLoaded())
    {
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT, ImVec2(titleBarHeight, titleBarHeight)))
        {
            gameInstance_->ResetAll();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Reset");

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CAMERA, ImVec2(titleBarHeight, titleBarHeight)))
        {
            gameInstance_->GetEngine().AddTickedTask([this](double deltaSeconds) -> bool
            {
                gameInstance_->GetEngine().RequestScreenShot("");
                return true;
            });
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Take a Screenshot into the screenshots folder");

        // Per-part mode toggle (only show if the file has build steps)
        if (gameInstance_->HasBuildSteps())
        {
            ImGui::SameLine();
            ImGui::GetForegroundDrawList()->AddLine(
                ImGui::GetCursorPos() + ImVec2(4, titleBarHeight / 2 - 5),
                ImGui::GetCursorPos() + ImVec2(4, titleBarHeight / 2 + 5),
                IM_COL32(255, 255, 255, 160), 2.0f);
            ImGui::SameLine(0.0f, 8.0f);

            bool perPart = gameInstance_->IsPerPartMode();
            if (ImGui::Button(perPart ? ICON_FA_CUBE : ICON_FA_LAYER_GROUP,
                              ImVec2(titleBarHeight, titleBarHeight)))
            {
                gameInstance_->SetPerPartMode(!perPart);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(perPart ? "Part Mode (click for Step Mode)" : "Step Mode (click for Part Mode)");
        }

        // Physics debug toggle
        ImGui::SameLine();
        ImGui::GetForegroundDrawList()->AddLine(
            ImGui::GetCursorPos() + ImVec2(4, titleBarHeight / 2 - 5),
            ImGui::GetCursorPos() + ImVec2(4, titleBarHeight / 2 + 5),
            IM_COL32(255, 255, 255, 160), 2.0f);
        ImGui::SameLine(0.0f, 8.0f);

        bool debugOn = gameInstance_->IsShowPhysicsDebug();
        if (debugOn)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.0f, 0.6f));
        if (ImGui::Button(ICON_FA_VECTOR_SQUARE, ImVec2(titleBarHeight, titleBarHeight)))
        {
            gameInstance_->TogglePhysicsDebug();
        }
        if (debugOn)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Physics Debug (F1)");

        ImGui::SameLine();
        bool globalPhysicsOn = gameInstance_->IsShowGlobalPhysicsBodies();
        if (globalPhysicsOn)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.1f, 0.1f, 0.75f));
        if (ImGui::Button(ICON_FA_CUBES_STACKED, ImVec2(titleBarHeight, titleBarHeight)))
        {
            gameInstance_->ToggleGlobalPhysicsBodies();
        }
        if (globalPhysicsOn)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Engine Physics Bodies Debug (F8)");

        ImGui::SameLine();
        bool snapDebugOn = gameInstance_->IsShowSnapDebug();
        if (snapDebugOn)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.45f, 0.0f, 0.75f));
        if (ImGui::Button("Sn", ImVec2(titleBarHeight, titleBarHeight)))
        {
            gameInstance_->ToggleSnapDebug();
        }
        if (snapDebugOn)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Snap Debug (F7)");

        ImGui::SameLine();
        ImGui::GetForegroundDrawList()->AddLine(
            ImGui::GetCursorPos() + ImVec2(4, titleBarHeight / 2 - 5),
            ImGui::GetCursorPos() + ImVec2(4, titleBarHeight / 2 + 5),
            IM_COL32(255, 255, 255, 160), 2.0f);
        ImGui::SameLine(0.0f, 8.0f);

        bool horizontalDrag = gameInstance_->IsHorizontalDragPlane();
        if (horizontalDrag)
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 0.7f, 0.75f));
        if (ImGui::Button(horizontalDrag ? ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT : ICON_FA_ARROW_UP,
                          ImVec2(titleBarHeight, titleBarHeight)))
        {
            gameInstance_->ToggleDragPlaneMode();
            gameInstance_->SwitchDragPlaneWhileDragging();
        }
        if (horizontalDrag)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(horizontalDrag ? "Drag: XZ Plane (F9)" : "Drag: Screen Plane (F9)");

        ImGui::SameLine();
        bool bgmPaused = gameInstance_->IsBGMPaused();
        if (ImGui::Button(bgmPaused ? ICON_FA_VOLUME_LOW : ICON_FA_VOLUME_XMARK, ImVec2(titleBarHeight, titleBarHeight)))
        {
            gameInstance_->PauseBGM(!bgmPaused);
        }
        if (ImGui::IsItemHovered())
        {
            const std::string currentBgmName = gameInstance_->GetCurrentBGMName();
            const std::string tooltip = currentBgmName.empty()
                ? std::string(bgmPaused ? "Play BGM" : "Pause BGM")
                : std::string(bgmPaused ? "Play BGM\n" : "Pause BGM\n") + currentBgmName;
            ImGui::SetTooltip("%s", tooltip.c_str());
        }

        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SHUFFLE, ImVec2(titleBarHeight, titleBarHeight)))
        {
            gameInstance_->PlayNextBGM();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Shuffle BGM");
    }
    titleBarLeftReservedWidth = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;

    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(4);
    gameInstance_->GetEngine().ConfigureCustomTitleBarDrag(
        true, titleBarHeight, titleBarLeftReservedWidth, titleBarRightReservedWidth);
}

void BrickPlayerUserInterface::RenderTimeline()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float panelHeight = 50.0f;
    float panelY = viewport->Pos.y + viewport->Size.y - panelHeight;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, panelY));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, panelHeight));
    ImGui::SetNextWindowBgAlpha(0.8f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##Timeline", nullptr, flags))
    {
        int32_t totalSteps = gameInstance_->GetTotalSteps();
        int32_t currentStep = gameInstance_->GetCurrentStep();
        bool isPlaying = gameInstance_->IsAutoPlaying();

        // Step backward button
        if (ImGui::Button(ICON_FA_BACKWARD_STEP))
        {
            gameInstance_->StepBackward();
        }
        ImGui::SameLine();

        // Play / Pause button
        if (ImGui::Button(isPlaying ? ICON_FA_PAUSE : ICON_FA_PLAY))
        {
            gameInstance_->ToggleAutoPlay();
        }
        ImGui::SameLine();

        // Speed button
        char speedLabel[16];
        snprintf(speedLabel, sizeof(speedLabel), "%s", gameInstance_->GetPlaySpeedLabel());
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, ImGui::GetStyle().FramePadding.y));
        if (ImGui::Button(speedLabel))
        {
            gameInstance_->CyclePlaySpeed();
        }
        ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Playback Speed");
        ImGui::SameLine();

        // Step forward button
        if (ImGui::Button(ICON_FA_FORWARD_STEP))
        {
            gameInstance_->StepForward();
        }
        ImGui::SameLine();

        // Timeline slider
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 260.0f);
        int step = currentStep;
        if (totalSteps > 0 && ImGui::SliderInt("##Step", &step, 0, std::max(0, totalSteps - 1)))
        {
            gameInstance_->SetCurrentStep(step);
        }
        ImGui::SameLine();

        // Step counter
        if (gameInstance_->IsPerPartMode())
            ImGui::Text("Part %d / %d", currentStep + 1, totalSteps);
        else
            ImGui::Text("Step %d / %d", currentStep + 1, totalSteps);
        ImGui::SameLine();

        // Disassemble button
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        bool hasSelection = gameInstance_->HasSelection();
        if (!hasSelection)
            ImGui::BeginDisabled();

        if (ImGui::Button(ICON_FA_HAMMER " Disassemble"))
        {
            gameInstance_->DisassembleSelected();
        }

        if (!hasSelection)
            ImGui::EndDisabled();
    }
    ImGui::End();
}

void BrickPlayerUserInterface::RenderFreeBuildToolbar()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    float panelHeight = 50.0f;
    float panelY = viewport->Pos.y + viewport->Size.y - panelHeight;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, panelY));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, panelHeight));
    ImGui::SetNextWindowBgAlpha(0.8f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##FreeBuild", nullptr, flags))
    {
        ImGui::Text(ICON_FA_CUBES " FreeBuild");
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::Button(ICON_FA_PLUS " Spawn Bricks (N)"))
        {
            gameInstance_->SpawnRandomBricks(6);
        }

        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();
        ImGui::Text("Available: %d", gameInstance_->CountAvailableBricks());
    }
    ImGui::End();
}

void BrickPlayerUserInterface::RenderWelcomeScreen()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                           viewport->Pos.y + viewport->Size.y * 0.5f);

    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 120));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    if (ImGui::Begin("##Welcome", nullptr, flags))
    {
        ImGui::Text("BrickPlayer");
        ImGui::Separator();
        ImGui::TextWrapped("Open an LDraw file (.ldr / .mpd) to begin, or start FreeBuild mode.");
        ImGui::Spacing();

        float buttonWidth = 120.0f;
        float totalWidth = buttonWidth * 2.0f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX((400.0f - totalWidth) * 0.5f);
        if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open File", ImVec2(buttonWidth, 0)))
        {
            gameInstance_->OpenFileDialog();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CUBES " FreeBuild", ImVec2(buttonWidth, 0)))
        {
            gameInstance_->StartFreeBuild();
        }
    }
    ImGui::End();
}
