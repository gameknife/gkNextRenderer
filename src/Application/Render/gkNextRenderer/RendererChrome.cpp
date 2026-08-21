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
#include "Engine/Runtime/Interface/ScreenShotService.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Modules/NextUI/ImGuiScaling.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/DevTools/UI/DeveloperStatusBar.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"
#include "Engine/Runtime/Utilities/NextEngineHelper.hpp"
#include "Modules/DevTools/GraphicsDebugPanel.hpp"
#include "Modules/DevTools/Command/DeleteNodesCommand.hpp"
#include "Modules/DevTools/Command/DuplicateNodesCommand.hpp"
#include "Modules/LiveCoding/LiveCodingModule.hpp"
#include "Engine/Utilities/Localization.hpp"
#include "Engine/Utilities/Format.hpp"
#include "Engine/Utilities/Math.hpp"
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
extern float TitlebarRightInfoWidth;

void NextRendererGameInstance::DrawTitleBar(const FGameUiFrameContext& context, FRendererUiState& uiState)
{
    NextUI::Theme::FAppTitleBarConfig config{};
    config.BrandWindowId = "RendererBrand";
    config.MenuWindowId = "RendererMenuBar";
    config.RightWindowId = "RendererWindowControls";
    config.AppName = "gkNextRenderer";
    config.Height = TitlebarSize;
    config.RightContentWidth = TitlebarRightInfoWidth;
    config.IsMaximized = GetEngine().IsMaximized();
    config.DrawMenuBar = [&]() -> float
    {
        float menuRight = ImGui::GetCursorScreenPos().x;

        const auto UpdateMenuRight = [&menuRight]()
        {
            menuRight = std::max(menuRight, ImGui::GetItemRectMax().x);
        };

        if (ImGui::BeginMenu("File"))
        {
            UpdateMenuRight();
            if (ImGui::MenuItem("Project Page"))
            {
                NextRenderer::OSCommand("https://github.com/gameknife/gkNextRenderer");
            }
            if (ImGui::MenuItem("Open Screenshot Folder"))
            {
                GetEngine().GetScreenShotService().EnsureDirectory();
                const std::string folderPath = GetEngine().GetScreenShotService().GetDirectory();
                NextRenderer::OSCommand(folderPath.c_str());
            }
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("View"))
        {
            UpdateMenuRight();
            auto& showFlags = GetEngine().GetShowFlags();
            Utilities::UI::DrawShowFlagsCommon(showFlags);
            ImGui::MenuItem("Statistics Overlay", nullptr, &GetEngine().GetUserSettings().ShowOverlay);
#if GK_WITH_VITURE
            if (HasVitureDebugPanel())
            {
                ImGui::MenuItem("VITURE AR Debug", nullptr, &GetVitureDebugPanelVisible());
            }
#endif
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Screenshot"))
        {
            UpdateMenuRight();
            if (ImGui::MenuItem("Screenshot"))
            {
                RequestScreenshot(false, "");
            }
            if (ImGui::MenuItem("Screenshot and Open Folder"))
            {
                RequestScreenshot(true, "");
            }
            ImGui::Separator();
            DrawVideoCaptureMenuItems();
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Renderer"))
        {
            UpdateMenuRight();
            Runtime::GraphicsDebugPanel::DrawRendererSelector(GetEngine(), GetEngine().GetUserSettings(),
                                                              "##RendererMenuSelector", 180.0f);
            bool referenceMode = GetEngine().GetOptions().ReferenceMode;
            if (ImGui::MenuItem("Reference Comparison", nullptr, &referenceMode))
            {
                GetEngine().SetReferenceMode(referenceMode);
            }
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Settings"))
        {
            UpdateMenuRight();
            ImGui::MenuItem("Render Settings", nullptr, &uiState.showSettings);
            ImGui::MenuItem("Stats Overlay", nullptr, &GetEngine().GetUserSettings().ShowOverlay);
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        if (ImGui::BeginMenu("Help"))
        {
            UpdateMenuRight();
            if (ImGui::MenuItem("Keyboard & Mouse"))
            {
                uiState.showCheatSheet = true;
            }
            if (ImGui::MenuItem("Documentation"))
            {
                SDL_OpenURL(Utilities::UI::ProjectDocsUrl);
            }
            if (ImGui::MenuItem("Report an Issue"))
            {
                SDL_OpenURL(Utilities::UI::ProjectIssuesUrl);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("About gkNextRenderer"))
            {
                uiState.showAbout = true;
            }
            ImGui::EndMenu();
        }
        else
        {
            UpdateMenuRight();
        }

        return menuRight;
    };
    config.OnMinimize = context.allowWindowCommands ? std::function<void()>([&]() { GetEngine().RequestMinimize(); })
                                                     : std::function<void()>();
    config.OnToggleMaximize = context.allowWindowCommands ? std::function<void()>([&]() { GetEngine().ToggleMaximize(); })
                                                          : std::function<void()>();
    config.OnClose = context.allowWindowCommands ? std::function<void()>([&]() { GetEngine().RequestClose(); })
                                                 : std::function<void()>();
    NextUI::Theme::DrawAppTitleBar(GetEngine(), config);
}

void NextRendererGameInstance::DrawBottomStatusBar()
{
    Runtime::DevToolsUI::DrawDeveloperStatusBar(GetEngine(), "RendererStatusBar", 30.0f,
                                         []() { Modules::LiveCoding::RequestCppReload(); },
                                         Modules::LiveCoding::IsCppLiveCodingAvailable(), false,
                                         [this]() { return DrawGiBakeIndicator(); });
}

bool NextRendererGameInstance::DrawGiBakeIndicator()
{
    const auto DrawActivity = [](const char* label, const std::string& value, float fraction,
                                 bool indeterminate, const char* tooltip)
    {
        constexpr float height = 20.0f;
        constexpr float rounding = 7.0f;
        constexpr float progressWidth = 76.0f;
        constexpr float progressHeight = 4.0f;
        constexpr float textScale = 0.78f;

        const ImVec2 position = ImGui::GetCursorScreenPos();
        ImFont* font = ImGui::GetFont();
        const float textSize = ImGui::GetFontSize() * textScale;
        const ImVec2 labelSize = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, label);
        const ImVec2 valueSize = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, value.c_str());
        const ImVec2 size(22.0f + labelSize.x + 10.0f + progressWidth + 8.0f + valueSize.x + 10.0f, height);
        const ImVec2 maximum = position + size;
        const ImVec2 progressMin(position.x + 22.0f + labelSize.x + 10.0f,
                                 position.y + (height - progressHeight) * 0.5f);
        const ImVec2 progressMax(progressMin.x + progressWidth, progressMin.y + progressHeight);
        ImDrawList* drawList = ImGui::GetWindowDrawList();

        drawList->AddRectFilled(position, maximum,
                                NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::SurfaceElevated, 0.90f),
                                rounding);
        drawList->AddRect(position, maximum,
                          NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::Accent, 0.32f), rounding);
        drawList->AddCircleFilled(ImVec2(position.x + 11.0f, position.y + height * 0.5f), 3.0f,
                                  NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::Accent));
        const float textY = position.y + (height - textSize) * 0.5f;
        drawList->AddText(font, textSize, ImVec2(position.x + 22.0f, textY),
                          NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::TextMuted), label);
        drawList->AddRectFilled(progressMin, progressMax,
                                NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::Background, 0.85f),
                                progressHeight * 0.5f);

        if (indeterminate)
        {
            constexpr float segmentWidth = 22.0f;
            const float phase = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.9f, 1.0f);
            const float segmentStart = progressMin.x - segmentWidth +
                phase * (progressWidth + segmentWidth * 2.0f);
            drawList->PushClipRect(progressMin, progressMax, true);
            drawList->AddRectFilled(ImVec2(segmentStart, progressMin.y),
                                    ImVec2(segmentStart + segmentWidth, progressMax.y),
                                    NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::AccentHover),
                                    progressHeight * 0.5f);
            drawList->PopClipRect();
        }
        else
        {
            const float fillWidth = std::max(progressHeight, progressWidth * std::clamp(fraction, 0.0f, 1.0f));
            drawList->AddRectFilled(progressMin, ImVec2(progressMin.x + fillWidth, progressMax.y),
                                    NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::AccentHover),
                                    progressHeight * 0.5f);
        }

        drawList->AddText(font, textSize, ImVec2(progressMax.x + 8.0f, textY),
                          NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::TextDim), value.c_str());

        ImGui::Dummy(size);
        if (ImGui::IsItemHovered())
        {
            NextUI::Theme::DrawTooltip(tooltip);
        }
    };

    const Assets::CPU::FProbeBakeProgress probeProgress =
        GetEngine().GetScene().GetCPUAccelerationStructure().GetProbeBakeProgress();
    const auto FormatProgressCount = [](uint32_t count)
    {
        return Utilities::metricFormatter(static_cast<double>(count), "");
    };
    if (probeProgress.stage == Assets::CPU::EProbeBakeStage::VoxelData)
    {
        const float fraction = probeProgress.totalVoxelGroups > 0u
            ? static_cast<float>(probeProgress.completedVoxelGroups) /
                  static_cast<float>(probeProgress.totalVoxelGroups)
            : 0.0f;
        DrawActivity("Voxel data", fmt::format("{} / {}", FormatProgressCount(probeProgress.completedVoxelGroups),
                                                FormatProgressCount(probeProgress.totalVoxelGroups)),
                     fraction, false, "CPU voxel data generation");
        return true;
    }

    if (probeProgress.stage == Assets::CPU::EProbeBakeStage::DistanceField)
    {
        DrawActivity("Distance field", "Building", 0.0f, true, "Rebuilding the voxel distance field");
        return true;
    }

    const Vulkan::FAmbientBakeProgress ambientProgress = GetEngine().GetRenderer().GetAmbientBakeProgress();
    if (!ambientProgress.active)
    {
        return false;
    }

    const float fraction = ambientProgress.totalDispatchGroups > 0u
        ? static_cast<float>(ambientProgress.completedDispatchGroups) /
              static_cast<float>(ambientProgress.totalDispatchGroups)
        : 0.0f;
    DrawActivity("Ambient bake", fmt::format("{} / {}", FormatProgressCount(ambientProgress.completedDispatchGroups),
                                              FormatProgressCount(ambientProgress.totalDispatchGroups)),
                 fraction, false,
                 "GPU ambient cube lighting bake: dispatched groups / total groups");
    return true;
}
