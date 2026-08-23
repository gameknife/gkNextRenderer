#include "Engine/Common/CoreMinimal.hpp"

#include "DeveloperStatusBar.hpp"
#include "TracyProfiler.hpp"

#include "Modules/NextUI/UI/AppChrome.hpp"
#include "Modules/NextUI/UI/DesktopUI.hpp"
#include "Modules/NextUI/UI/UiScopes.hpp"
#include "Modules/NextUI/UI/UiTheme.hpp"
#include "Modules/NextUI/UI/UiWidgets.hpp"
#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Modules/DevTools/RenderDoc.hpp"
#include "Engine/Utilities/Format.hpp"
#include "Modules/DevTools/UiDevPanels.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <algorithm>
#include <cmath>

namespace Runtime::DevToolsUI
{
    namespace
    {
        void DrawSeparator()
        {
            ImGui::SameLine(0.0f, 10.0f);
            const ImVec2 position = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(position.x, position.y + 3.0f),
                ImVec2(position.x, position.y + 17.0f),
                NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::Border, 0.72f));
            ImGui::Dummy(ImVec2(1.0f, 20.0f));
            ImGui::SameLine(0.0f, 10.0f);
        }

        bool StatusButton(const char* label, const char* tooltip, const bool active, const ImVec2 size)
        {
            NextUI::Foundation::FButtonOptions options;
            options.variant = NextUI::Foundation::EButtonVariant::Ghost;
            options.size = size;
            options.tooltip = tooltip;
            options.active = active;
            return NextUI::Foundation::Button(label, options);
        }

        bool StatusIconButton(const char* icon, const char* tooltip, const bool active, const ImVec2 size,
                              const bool activeUnderline = false)
        {
            return NextUI::Foundation::IconButton(icon, tooltip, active, size, activeUnderline);
        }

        enum class EBakeStatus
        {
            Disabled,
            Progress,
            Complete,
        };

        void DrawBakeActivity(NextEngine& engine)
        {
            const auto DrawActivity = [](const char* label, const char* value, const float fraction,
                                         const bool indeterminate, const EBakeStatus status, const char* tooltip)
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
                const ImVec2 valueSize = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, value);
                const ImVec2 size(22.0f + labelSize.x + 10.0f + progressWidth + 8.0f + valueSize.x + 10.0f,
                                  height);
                const ImVec2 maximum = position + size;
                const ImVec2 progressMin(position.x + 22.0f + labelSize.x + 10.0f,
                                         position.y + (height - progressHeight) * 0.5f);
                const ImVec2 progressMax(progressMin.x + progressWidth, progressMin.y + progressHeight);
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const NextUI::Foundation::EColor statusColor = status == EBakeStatus::Disabled
                    ? NextUI::Foundation::EColor::TextDim
                    : status == EBakeStatus::Complete
                        ? NextUI::Foundation::EColor::Success
                        : NextUI::Foundation::EColor::AccentHover;

                drawList->AddRectFilled(position, maximum,
                                        NextUI::Foundation::ColorU32(
                                            NextUI::Foundation::EColor::SurfaceElevated, 0.90f),
                                        rounding);
                drawList->AddRect(position, maximum,
                                  NextUI::Foundation::ColorU32(
                                      statusColor, status == EBakeStatus::Disabled ? 0.20f : 0.32f),
                                  rounding);
                drawList->AddCircleFilled(ImVec2(position.x + 11.0f, position.y + height * 0.5f), 3.0f,
                                          NextUI::Foundation::ColorU32(statusColor));
                const float textY = position.y + (height - textSize) * 0.5f;
                drawList->AddText(font, textSize, ImVec2(position.x + 22.0f, textY),
                                  NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::TextMuted), label);
                drawList->AddRectFilled(progressMin, progressMax,
                                        NextUI::Foundation::ColorU32(
                                            NextUI::Foundation::EColor::Background, 0.85f),
                                        progressHeight * 0.5f);

                if (status != EBakeStatus::Disabled && indeterminate)
                {
                    constexpr float segmentWidth = 22.0f;
                    const float phase = std::fmod(static_cast<float>(ImGui::GetTime()) * 0.9f, 1.0f);
                    const float segmentStart = progressMin.x - segmentWidth +
                        phase * (progressWidth + segmentWidth * 2.0f);
                    drawList->PushClipRect(progressMin, progressMax, true);
                    drawList->AddRectFilled(ImVec2(segmentStart, progressMin.y),
                                            ImVec2(segmentStart + segmentWidth, progressMax.y),
                                            NextUI::Foundation::ColorU32(statusColor),
                                            progressHeight * 0.5f);
                    drawList->PopClipRect();
                }
                else if (status != EBakeStatus::Disabled)
                {
                    const float fillWidth = std::max(
                        progressHeight, progressWidth * std::clamp(fraction, 0.0f, 1.0f));
                    drawList->AddRectFilled(progressMin, ImVec2(progressMin.x + fillWidth, progressMax.y),
                                            NextUI::Foundation::ColorU32(statusColor),
                                            progressHeight * 0.5f);
                }

                drawList->AddText(font, textSize, ImVec2(progressMax.x + 8.0f, textY),
                                  NextUI::Foundation::ColorU32(statusColor), value);

                ImGui::Dummy(size);
                if (ImGui::IsItemHovered())
                {
                    NextUI::Theme::DrawTooltip(tooltip);
                }
            };

            auto& renderer = engine.GetRenderer();
            const auto rendererRequirements = renderer.ActiveRendererRequirements();
            if (!rendererRequirements.requestAmbientCube || renderer.ShouldSkipAmbientCubeUpdates())
            {
                DrawActivity("Bake", "Disabled", 0.0f, false, EBakeStatus::Disabled,
                             "Ambient cube bake is disabled for the active renderer");
                return;
            }

            const Assets::CPU::FProbeBakeProgress probeProgress =
                engine.GetScene().GetCPUAccelerationStructure().GetProbeBakeProgress();
            if (probeProgress.stage == Assets::CPU::EProbeBakeStage::VoxelData)
            {
                const float fraction = probeProgress.totalVoxelGroups > 0u
                    ? static_cast<float>(probeProgress.completedVoxelGroups) /
                          static_cast<float>(probeProgress.totalVoxelGroups)
                    : 0.0f;
                DrawActivity("Bake", "Progress", fraction, false, EBakeStatus::Progress,
                             "Bake progress: CPU voxel data generation");
                return;
            }

            if (probeProgress.stage == Assets::CPU::EProbeBakeStage::DistanceField)
            {
                DrawActivity("Bake", "Progress", 0.0f, true, EBakeStatus::Progress,
                             "Bake progress: rebuilding the voxel distance field");
                return;
            }

            const Vulkan::FAmbientBakeProgress ambientProgress = renderer.GetAmbientBakeProgress();
            if (ambientProgress.active)
            {
                const float fraction = ambientProgress.totalDispatchGroups > 0u
                    ? static_cast<float>(ambientProgress.completedDispatchGroups) /
                          static_cast<float>(ambientProgress.totalDispatchGroups)
                    : 0.0f;
                DrawActivity("Bake", "Progress", fraction, false, EBakeStatus::Progress,
                             "Bake progress: GPU ambient cube lighting");
                return;
            }

            DrawActivity("Bake", "Complete", 1.0f, false, EBakeStatus::Complete,
                         "Ambient cube bake complete");
        }
    }

    void DrawDeveloperStatusBar(NextEngine& engine,
                                const char* windowId,
                                const float height,
                                std::function<void()> onCppReloadClicked,
                                const bool cppLiveCodingAvailable,
                                const bool detachedStatisticsViewport)
    {
        const NextEngine::FHotReloadStatus hotReload = engine.GetHotReloadStatus();
        const auto memory = engine.GetRenderer().Device().CaptureMemoryStats();
        const bool shaderLive = hotReload.shaderHotReloadEnabled && hotReload.shaderInitialized;
        const std::string fpsText = fmt::format("FPS {:.0f}", engine.GetFrameRate());
        const std::string memoryText = fmt::format(
            "VRAM {} / {}", Utilities::FormatBytes(memory.deviceLocalUsageBytes),
            Utilities::FormatBytes(memory.deviceLocalBudgetBytes));

        constexpr float consoleWidth = 74.0f;
        constexpr float buttonHeight = 22.0f;
        constexpr float toolWidth = 24.0f;
#if WITH_RENDERDOC
        const bool renderDocSupported = Runtime::RenderDoc::IsSupported();
#else
        constexpr bool renderDocSupported = false;
#endif
        const float fpsWidth = ImGui::CalcTextSize(fpsText.c_str()).x + 12.0f;
        const float memoryWidth = ImGui::CalcTextSize(memoryText.c_str()).x + 12.0f;
        const float rightWidth = consoleWidth + toolWidth * (3.0f + (renderDocSupported ? 1.0f : 0.0f)) +
            fpsWidth + memoryWidth + 98.0f + (renderDocSupported ? 20.0f : 0.0f);

        NextUI::Foundation::FBottomBarOptions options;
        options.windowId = windowId;
        options.height = height;
        options.rightWidth = rightWidth;
        options.drawLeftContent = [&engine]()
        {
            DrawBakeActivity(engine);
        };
        options.drawRightContent = [&]()
        {
            DevTools::FUiDevPanels& panels = DevTools::FUiDevPanels::Get();
            panels.SetStatisticsDetachedViewport(detachedStatisticsViewport);
            if (StatusButton("Console", "Toggle Console", panels.IsConsoleOpen(),
                             ImVec2(consoleWidth, buttonHeight)))
            {
                panels.ToggleConsole();
            }
            DrawSeparator();
            if (StatusIconButton(ICON_FA_WAND_MAGIC_SPARKLES "##BottomBarRebuildShaders",
                                 shaderLive ? "Rebuild shaders now (hot reload ready)" : "Rebuild shaders now",
                                 shaderLive, ImVec2(toolWidth, buttonHeight), true))
            {
                engine.RequestShaderHotReload();
            }
            ImGui::SameLine(0.0f, 4.0f);
            {
                NextUI::Foundation::FScopedDisabled disabled(!cppLiveCodingAvailable || !onCppReloadClicked);
                if (StatusIconButton(ICON_FA_HAMMER "##BottomBarCompileCpp",
                                     cppLiveCodingAvailable ? "Compile C++ changes" : "Live++ is unavailable",
                                     cppLiveCodingAvailable, ImVec2(toolWidth, buttonHeight)) && onCppReloadClicked)
                {
                    onCppReloadClicked();
                }
            }
            ImGui::SameLine(0.0f, 4.0f);
            {
                const bool tracyAvailable = IsTracyProfilerAvailable();
                NextUI::Foundation::FScopedDisabled disabled(!tracyAvailable);
                if (StatusIconButton(ICON_FA_CHART_LINE "##BottomBarTracy",
                                     tracyAvailable ? "Start Tracy and connect to 127.0.0.1:8086"
                                                    : "Tracy is unavailable; run `gnb tracy fetch`",
                                     false, ImVec2(toolWidth, buttonHeight)) && tracyAvailable)
                {
                    LaunchTracyProfiler();
                }
            }

            ImGui::SameLine(0.0f, 4.0f);
            {
                NextUI::Foundation::FScopedDisabled disabled(!renderDocSupported);
                if (StatusIconButton(ICON_FA_CAMERA "##BottomBarRenderDocCapture",
                                     renderDocSupported ? "Capture frame and open RenderDoc"
                                                         : "RenderDoc is unavailable; launch with --renderdoc",
                                     false, ImVec2(toolWidth, buttonHeight)) && renderDocSupported)
                {
                    Runtime::RenderDoc::RequestCapture();
                }
            }

            DrawSeparator();
            if (StatusButton(fpsText.c_str(), "Toggle Stats Overlay", engine.GetUserSettings().ShowOverlay,
                             ImVec2(fpsWidth, buttonHeight)))
            {
                engine.GetUserSettings().ShowOverlay = !engine.GetUserSettings().ShowOverlay;
            }
            DrawSeparator();
            if (StatusButton(memoryText.c_str(), "Show VRAM details", panels.IsMemoryStatisticsOpen(),
                             ImVec2(memoryWidth, buttonHeight)))
            {
                panels.ToggleMemoryStatistics();
            }
        };
        NextUI::Foundation::DrawBottomBar(options);
    }
}
