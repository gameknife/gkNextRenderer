#include "gkNextRenderer.hpp"

#include <imgui.h>
#include <ThirdParty/fontawesome/IconsFontAwesome6.h>

#include <algorithm>
#include <array>
#include <random>
#include <tuple>

#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/ScreenShotService.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Editor/ImGuiScaling.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Engine/Runtime/Editor/UI/DesktopUI.hpp"
#include "Modules/DevTools/UI/DeveloperStatusBar.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/Format.hpp"
#include "Engine/Utilities/AboutDialog.hpp"
#include "Engine/Utilities/ImGui.hpp"

#include <SDL3/SDL_misc.h>
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Vulkan/Allocator.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Application/Common/DemoScenes.hpp"


extern float TitlebarSize;
extern float ModeRailWidth;
extern float ModeRailButtonSize;

namespace RendererViewportDetail
{
using NextUI::Theme::DrawFlatViewportButton;
using NextUI::Theme::DrawViewportComboOption;
using NextUI::Theme::PopViewportPopupStyle;
using NextUI::Theme::PopViewportToolbarStyle;
using NextUI::Theme::PushViewportPopupStyle;
using NextUI::Theme::PushViewportToolbarStyle;

enum class ESceneListGroup : uint8_t
{
    Procedural = 0,
    Gltf = 1,
    LDraw = 2,
    Other = 3,
};

ESceneListGroup GetSceneListGroup(std::string_view scenePath)
{
    const std::string extension = std::filesystem::path(scenePath).extension().string();
    if (extension == ".proc")
    {
        return ESceneListGroup::Procedural;
    }
    if (extension == ".glb" || extension == ".gltf")
    {
        return ESceneListGroup::Gltf;
    }
    if (extension == ".ldr" || extension == ".mpd")
    {
        return ESceneListGroup::LDraw;
    }
    return ESceneListGroup::Other;
}

const char* GetSceneListGroupLabel(ESceneListGroup group)
{
    switch (group)
    {
    case ESceneListGroup::Procedural:
        return "Procedural";
    case ESceneListGroup::Gltf:
        return "glTF";
    case ESceneListGroup::LDraw:
        return "OMR/LDraw";
    case ESceneListGroup::Other:
    default:
        return "Other";
    }
}


} // namespace RendererViewportDetail

void NextRendererGameInstance::DrawModeRail(FRendererUiState& uiState)
{
    using namespace RendererViewportDetail;
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 railPos = viewport->Pos + ImVec2(0.0f, TitlebarSize);
    const ImVec2 railSize = ImVec2(ModeRailWidth, viewport->Size.y - TitlebarSize - 30.0f);

    ImDrawList* background = ImGui::GetBackgroundDrawList();
    background->AddRectFilled(railPos, railPos + railSize,
                              NextUI::Theme::ColorU32(NextUI::Theme::EColor::Background));
    background->AddLine(ImVec2(railPos.x + railSize.x - 1.0f, railPos.y),
                        ImVec2(railPos.x + railSize.x - 1.0f, railPos.y + railSize.y),
                        NextUI::Theme::ColorU32(NextUI::Theme::EColor::Border));

    ImGui::SetNextWindowPos(railPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(railSize, ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2((ModeRailWidth - ModeRailButtonSize) * 0.5f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("##ModeRail", nullptr, flags))
    {
        struct ModeEntry
        {
            EWorkMode mode;
            const char* icon;
            const char* tooltip;
        };
        const ModeEntry topEntries[] = {
            {EWorkMode::Render,  ICON_FA_EYE,        "Render - Hide All Panels"},
            {EWorkMode::Detail,  ICON_FA_SLIDERS,    "Detail - Renderer Settings"},
            {EWorkMode::Profile, ICON_FA_CHART_LINE, "Profile - Memory & Stats"},
        };

        for (const auto& entry : topEntries)
        {
            const bool active = (entry.mode == uiState.workMode);
            if (NextUI::Theme::ModeRailButton(entry.icon, entry.tooltip, active, ModeRailButtonSize))
            {
                uiState.workMode = entry.mode;
                uiState.lastWorkMode = EWorkMode::Count;
            }
        }

        // Push the CVar editor button to the bottom.
        const float cvarButtonSize = ModeRailButtonSize;
        const float spaceUntilBottom = ImGui::GetContentRegionAvail().y - cvarButtonSize - 6.0f;
        if (spaceUntilBottom > 0.0f)
        {
            ImGui::Dummy(ImVec2(0.0f, spaceUntilBottom));
        }
        const bool cvarActive = (uiState.workMode == EWorkMode::CVar) &&
            GetEngine().GetShowFlags().DebugCVarPanel;
        if (NextUI::Theme::ModeRailButton(
                ICON_FA_TERMINAL, "CVars - Runtime Configuration", cvarActive, cvarButtonSize))
        {
            uiState.workMode = cvarActive ? EWorkMode::Render : EWorkMode::CVar;
            uiState.lastWorkMode = EWorkMode::Count;
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(3);
}

void NextRendererGameInstance::DrawViewportTopBar(
    const FGameUiFrameContext&, FRendererUiState& uiState)
{
    using namespace RendererViewportDetail;
    Runtime::Config::UserSettings& userSetting = GetEngine().GetUserSettings();
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    constexpr float panelMargin = 10.0f;
    // Derived from the font so the bar keeps its proportions when the UI is scaled.
    const float toolbarHeight = std::ceil(ImGui::GetFontSize() + 22.0f);
    constexpr float rightToolbarWidth = 168.0f;
    const float leftEdge = viewport->Pos.x + ModeRailWidth +
        (uiState.showSettings ? (360.0f + panelMargin * 2.0f) : panelMargin);
    const float rightEdge = viewport->Pos.x + viewport->Size.x - panelMargin;
    const float topEdge = viewport->Pos.y + TitlebarSize + panelMargin;
    const float availableWidth = std::max(0.0f, rightEdge - leftEdge - rightToolbarWidth - panelMargin);
    const bool showSceneSelector = availableWidth >= 650.0f;
    const bool showUpscalerSelector = availableWidth >= 500.0f;

    std::string sceneLabel = "Scene";
    if (userSetting.SceneIndex >= 0 &&
        userSetting.SceneIndex < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size()))
    {
        sceneLabel = std::filesystem::path(
            Runtime::Scene::SceneList::AllScenes[userSetting.SceneIndex]).stem().string();
    }

    const auto& upscalerInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
        static_cast<uint32_t>(std::max(0, userSetting.UpscalerType)));
    const auto& upscaleModeInfo = Rendering::Upscaler::GetUpscaleModeInfo(userSetting.SuperResolution);
    const std::string upscalerLabel = fmt::format("{} · {}", upscalerInfo.name, upscaleModeInfo.name);

    // Item widths follow the widest label each control can display instead of magic
    // numbers, so nothing clips when the font size, DPI scale or locale changes.
    // The padding constants mirror PushViewportToolbarStyle's FramePadding.
    constexpr float toolbarFramePaddingX = 7.0f;
    constexpr float toolbarFramePaddingY = 3.0f;
    const float comboArrowWidth = ImGui::GetFontSize() + toolbarFramePaddingY * 2.0f;

    auto widestLabel = [](std::initializer_list<const char*> labels)
    {
        float widest = 0.0f;
        for (const char* label : labels)
        {
            widest = std::max(widest, ImGui::CalcTextSize(label).x);
        }
        return widest;
    };
    auto comboWidth = [&](const float textWidth)
    {
        return std::ceil(textWidth + toolbarFramePaddingX * 2.0f + comboArrowWidth);
    };
    auto buttonWidth = [&](const float textWidth)
    {
        return std::ceil(textWidth + toolbarFramePaddingX * 2.0f);
    };

    float widestRendererLabel = 0.0f;
    for (const auto& option : Rendering::RendererChoiceCatalog())
    {
        widestRendererLabel = std::max(widestRendererLabel, ImGui::CalcTextSize(option.displayName).x);
    }

    const float sceneWidth = showSceneSelector
        ? comboWidth(std::max(ImGui::CalcTextSize(sceneLabel.c_str()).x, ImGui::CalcTextSize("CornellBox").x))
        : 0.0f;
    const float rendererWidth = comboWidth(widestRendererLabel);
    const float renderModeWidth = buttonWidth(widestLabel({"Progressive", "Realtime"}));
    const float samplesWidth = comboWidth(widestLabel({"16 spp/frame"}));
    const float upscalerWidth = showUpscalerSelector
        ? comboWidth(ImGui::CalcTextSize(upscalerLabel.c_str()).x)
        : 0.0f;
    const float leftToolbarWidth = 8.0f + sceneWidth + rendererWidth + renderModeWidth +
        samplesWidth + upscalerWidth +
        (showSceneSelector ? 4.0f : 0.0f) + (showUpscalerSelector ? 4.0f : 0.0f) + 12.0f;

    NextUI::Theme::FOverlayPanelConfig leftConfig{};
    leftConfig.WindowId = "##ViewportRenderToolbar";
    leftConfig.Position = ImVec2(leftEdge, topEdge);
    leftConfig.Size = ImVec2(std::min(leftToolbarWidth, availableWidth), toolbarHeight);
    leftConfig.Padding = ImVec2(4.0f, 8.0f);
    leftConfig.ItemSpacing = ImVec2(4.0f, 0.0f);
    leftConfig.Rounding = 5.0f;
    leftConfig.BackgroundAlpha = 0.74f;

    if (NextUI::Theme::BeginOverlayPanel(leftConfig))
    {
        PushViewportToolbarStyle();
        if (showSceneSelector)
        {
            ImGui::SetNextItemWidth(sceneWidth);
            PushViewportPopupStyle();
            if (ImGui::BeginCombo("##ViewportScene", sceneLabel.c_str()))
            {
                ESceneListGroup currentGroup = ESceneListGroup::Other;
                bool hasGroup = false;
                for (int sceneIdx = 0;
                     sceneIdx < static_cast<int>(Runtime::Scene::SceneList::AllScenes.size());
                     ++sceneIdx)
                {
                    const std::string& scenePath = Runtime::Scene::SceneList::AllScenes[sceneIdx];
                    const ESceneListGroup sceneGroup = GetSceneListGroup(scenePath);
                    if (!hasGroup || sceneGroup != currentGroup)
                    {
                        if (hasGroup)
                        {
                            ImGui::Separator();
                        }
                        currentGroup = sceneGroup;
                        hasGroup = true;
                        ImGui::Dummy(ImVec2(0.0f, 2.0f));
                        ImGui::TextDisabled("%s", GetSceneListGroupLabel(sceneGroup));
                    }

                    const bool selected = sceneIdx == userSetting.SceneIndex;
                    const std::string label = std::filesystem::path(scenePath).filename().string();
                    if (DrawViewportComboOption(label.c_str(), selected))
                    {
                        userSetting.SceneIndex = sceneIdx;
                        GetEngine().RequestLoadScene({.filename = scenePath});
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            PopViewportPopupStyle();
            NextUI::Theme::DrawTooltip("Scene");
            ImGui::SameLine();
        }

        const int rendererOptionCount = Runtime::GraphicsDebugPanel::GetRendererOptionCount(GetEngine());
        int currentRendererIndex =
            Runtime::GraphicsDebugPanel::ResolveRendererOptionIndex(GetEngine(), userSetting, rendererOptionCount);
        if (currentRendererIndex < 0)
        {
            currentRendererIndex = 0;
            GetEngine().RequestRendererType(
                Runtime::GraphicsDebugPanel::GetRendererOption(GetEngine(), currentRendererIndex).type);
        }
        ImGui::SetNextItemWidth(rendererWidth);
        PushViewportPopupStyle();
        if (ImGui::BeginCombo(
                "##ViewportRenderer",
                Runtime::GraphicsDebugPanel::GetRendererOption(GetEngine(), currentRendererIndex).displayName))
        {
            for (int rendererIndex = 0; rendererIndex < rendererOptionCount; ++rendererIndex)
            {
                const bool selected = rendererIndex == currentRendererIndex;
                if (DrawViewportComboOption(
                        Runtime::GraphicsDebugPanel::GetRendererOption(GetEngine(), rendererIndex).displayName, selected))
                {
                    GetEngine().RequestRendererType(
                        Runtime::GraphicsDebugPanel::GetRendererOption(GetEngine(), rendererIndex).type);
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        PopViewportPopupStyle();
        NextUI::Theme::DrawTooltip("Renderer");
        ImGui::SameLine();

        const char* renderModeLabel = userSetting.ProgressiveRender ? "Progressive" : "Realtime";
        if (DrawFlatViewportButton(
                renderModeLabel, "Toggle realtime / progressive rendering",
                userSetting.ProgressiveRender, ImVec2(renderModeWidth, ImGui::GetFrameHeight())))
        {
            userSetting.ProgressiveRender = !userSetting.ProgressiveRender;
        }
        ImGui::SameLine();

        ImGui::SetNextItemWidth(samplesWidth);
        const int effectiveSamples = userSetting.ProgressiveRender ? 1 : userSetting.NumberOfSamples;
        const std::string sampleLabel = fmt::format("{} spp/frame", effectiveSamples);
        PushViewportPopupStyle();
        ImGui::BeginDisabled(userSetting.ProgressiveRender);
        if (ImGui::BeginCombo("##ViewportSamples", sampleLabel.c_str()))
        {
            for (int samples = 1; samples <= 16; ++samples)
            {
                const bool selected = samples == userSetting.NumberOfSamples;
                if (DrawViewportComboOption(
                        fmt::format("{} spp/frame", samples).c_str(), selected))
                {
                    userSetting.NumberOfSamples = samples;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        PopViewportPopupStyle();
        NextUI::Theme::DrawTooltip(userSetting.ProgressiveRender
            ? "Progressive rendering always uses 1 spp per frame"
            : "Samples traced per pixel, per rendered frame");

        if (showUpscalerSelector)
        {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(upscalerWidth);
            PushViewportPopupStyle();
            if (ImGui::BeginCombo("##ViewportUpscaler", upscalerLabel.c_str()))
            {
                ImGui::TextDisabled("Upscaler");
                for (uint32_t rawType = 0;
                     rawType < static_cast<uint32_t>(Rendering::Upscaler::EUpscalerType::Count);
                     ++rawType)
                {
                    const auto& typeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(rawType);
                    const bool supported = typeInfo.type == Rendering::Upscaler::EUpscalerType::None ||
                        GetEngine().GetRenderer().SupportsUpscaler(typeInfo.type);
                    const bool selected = rawType == static_cast<uint32_t>(userSetting.UpscalerType);
                    ImGui::BeginDisabled(!supported);
                    if (DrawViewportComboOption(typeInfo.name, selected))
                    {
                        GetEngine().SetUpscalerConfiguration(typeInfo.type, userSetting.SuperResolution);
                    }
                    ImGui::EndDisabled();
                }

                ImGui::Separator();
                ImGui::TextDisabled("Quality");
                for (uint32_t rawMode = 0;
                     rawMode <= static_cast<uint32_t>(Rendering::Upscaler::EUpscaleMode::Auto);
                     ++rawMode)
                {
                    const auto& modeInfo = Rendering::Upscaler::GetUpscaleModeInfo(rawMode);
                    const bool selected = rawMode == userSetting.SuperResolution;
                    if (DrawViewportComboOption(modeInfo.name, selected))
                    {
                        GetEngine().SetUpscalerConfiguration(
                            static_cast<Rendering::Upscaler::EUpscalerType>(userSetting.UpscalerType), rawMode);
                    }
                }
                ImGui::EndCombo();
            }
            PopViewportPopupStyle();
            NextUI::Theme::DrawTooltip("Upscaler and quality mode");
        }
        PopViewportToolbarStyle();
    }
    NextUI::Theme::EndOverlayPanel();

    NextUI::Theme::FOverlayPanelConfig rightConfig{};
    rightConfig.WindowId = "##ViewportActionToolbar";
    rightConfig.Position = ImVec2(rightEdge - rightToolbarWidth, topEdge);
    rightConfig.Size = ImVec2(rightToolbarWidth, toolbarHeight);
    rightConfig.Padding = ImVec2(4.0f, 8.0f);
    rightConfig.ItemSpacing = ImVec2(4.0f, 0.0f);
    rightConfig.Rounding = 5.0f;
    rightConfig.BackgroundAlpha = 0.74f;

    if (NextUI::Theme::BeginOverlayPanel(rightConfig))
    {
        PushViewportToolbarStyle();
        if (DrawFlatViewportButton(
                ICON_FA_ROTATE_LEFT, "Reset camera to the scene view", false, ImVec2(28.0f, 22.0f)))
        {
            modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
        }
        ImGui::SameLine();

        glm::vec3 focusCenter;
        float focusRadius = 0.0f;
        const bool hasSelection = GetEngine().GetScene().GetSelectedNodeBounds(focusCenter, focusRadius);
        ImGui::BeginDisabled(!hasSelection);
        if (DrawFlatViewportButton(
                ICON_FA_CROSSHAIRS, "Focus selected object", false, ImVec2(28.0f, 22.0f)))
        {
            modelViewController_.Focus(focusCenter, focusRadius);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        if (DrawFlatViewportButton(
                ICON_FA_CAMERA, "Take screenshot", false, ImVec2(28.0f, 22.0f)))
        {
            RequestScreenshot(false, "");
        }
        ImGui::SameLine();
        if (DrawFlatViewportButton(
                ICON_FA_CHEVRON_DOWN, "More capture options", false, ImVec2(24.0f, 22.0f)))
        {
            ImGui::OpenPopup("##ViewportCaptureMenu");
        }
        ImGui::SameLine();
        if (DrawFlatViewportButton(
                ICON_FA_KEYBOARD, "Toggle shortcut cheat sheet",
                uiState.showCheatSheet, ImVec2(28.0f, 22.0f)))
        {
            uiState.showCheatSheet = !uiState.showCheatSheet;
        }

        PushViewportPopupStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
        if (ImGui::BeginPopup("##ViewportCaptureMenu"))
        {
            if (ImGui::MenuItem("Screenshot and Open Folder"))
            {
                RequestScreenshot(true, "");
            }
            ImGui::Separator();
            DrawVideoCaptureMenuItems();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        PopViewportPopupStyle();
        PopViewportToolbarStyle();
    }
    NextUI::Theme::EndOverlayPanel();
}

namespace RendererViewportDetail
{
    // One row of the shortcut cheat sheet. A null action marks a section header.
    // Kept as data so the panel can size itself from the row count.
    struct FCheatSheetRow
    {
        const char* shortcut;
        const char* action;
    };

    constexpr std::array<FCheatSheetRow, 17> CheatSheetRows = {{
        {"NAVIGATION", nullptr},
        {"RMB + Drag", "Look around"},
        {"RMB + W A S D", "Move camera"},
        {"RMB + Q / E", "Move up / down"},
        {"Mouse Wheel", "Dolly forward / back"},
        {"Alt + RMB", "Orbit selected object"},
        {"SCENE & SELECTION", nullptr},
        {"LMB", "Select object / set focus"},
        {"F", "Focus selected object"},
        {"Space", "Launch a physics cube"},
        {"B", "Drop 400 physics spheres"},
        {"Esc", "Clear selection"},
        {"Ctrl / Cmd + D", "Duplicate selection"},
        {"Delete / Backspace", "Delete selection"},
        {"TRANSFORM GIZMO", nullptr},
        {"W / E / R", "Move / Rotate / Scale"},
        {"Q", "Toggle Local / World"},
    }};
}

void NextRendererGameInstance::DrawViewportCheatSheet(FRendererUiState& uiState)
{
    using namespace RendererViewportDetail;
    if (!uiState.showCheatSheet)
    {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    constexpr float panelMargin = 10.0f;
    constexpr float panelGap = 8.0f;
    constexpr float panelWidth = 390.0f;
    constexpr float bottomBarHeight = 30.0f;
    constexpr float cellPaddingY = 4.0f;
    const float toolbarHeight = std::ceil(ImGui::GetFontSize() + 22.0f);
    const float rightEdge = viewport->Pos.x + viewport->Size.x - panelMargin;
    const float topEdge = viewport->Pos.y + TitlebarSize + panelMargin + toolbarHeight + panelGap;

    // Height follows the row count and the current font, and is clamped to what the
    // viewport can show; the rows themselves live in a scrolling child so a short
    // window truncates nothing.
    const float rowHeight = ImGui::GetTextLineHeight() + cellPaddingY * 2.0f;
    const float headerHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0f + 8.0f;
    const float naturalHeight = std::ceil(
        10.0f * 2.0f + headerHeight + rowHeight * static_cast<float>(CheatSheetRows.size()));
    const float availableHeight = std::max(
        120.0f,
        viewport->Pos.y + viewport->Size.y - bottomBarHeight - panelMargin - topEdge);
    const float panelHeight = std::min(naturalHeight, availableHeight);

    NextUI::Theme::FOverlayPanelConfig config{};
    config.WindowId = "##ViewportShortcutCheatSheet";
    config.Position = ImVec2(rightEdge - panelWidth, topEdge);
    config.Size = ImVec2(panelWidth, panelHeight);
    config.Padding = ImVec2(14.0f, 10.0f);
    config.ItemSpacing = ImVec2(6.0f, 5.0f);
    config.Rounding = 6.0f;
    config.BackgroundAlpha = 0.90f;

    if (NextUI::Theme::BeginOverlayPanel(config))
    {
        ImGui::TextColored(
            NextUI::Theme::Color(NextUI::Theme::EColor::Text),
            "%s  Keyboard & Mouse", ICON_FA_KEYBOARD);

        const float closeButtonWidth = 24.0f;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - closeButtonWidth);
        PushViewportToolbarStyle();
        if (DrawFlatViewportButton(
                ICON_FA_XMARK, "Close shortcut cheat sheet", false, ImVec2(closeButtonWidth, 22.0f)))
        {
            uiState.showCheatSheet = false;
        }
        PopViewportToolbarStyle();

        NextUI::Theme::DrawThinSeparator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));

        const auto DrawSection = [](const char* title)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TableSetBgColor(
                ImGuiTableBgTarget_RowBg0,
                NextUI::Theme::ColorU32(NextUI::Theme::EColor::Surface, 0.55f));
            ImGui::TextColored(
                NextUI::Theme::Color(NextUI::Theme::EColor::TextDim), "%s", title);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted("");
        };

        const auto DrawShortcut = [](const char* shortcut, const char* action)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(
                NextUI::Theme::Color(NextUI::Theme::EColor::Text), "%s", shortcut);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(
                NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted), "%s", action);
        };

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, cellPaddingY));
        // The overlay panel itself is created with NoScrollbar; a child region gets its
        // own scrollbar so the last rows stay reachable in a short window.
        if (ImGui::BeginChild("##ViewportShortcutScroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None))
        {
            if (ImGui::BeginTable(
                    "##ViewportShortcuts", 2,
                    ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
            {
                ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthFixed, 136.0f);
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);

                for (const FCheatSheetRow& row : CheatSheetRows)
                {
                    if (row.action == nullptr)
                    {
                        DrawSection(row.shortcut);
                    }
                    else
                    {
                        DrawShortcut(row.shortcut, row.action);
                    }
                }

                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }
    NextUI::Theme::EndOverlayPanel();
}
