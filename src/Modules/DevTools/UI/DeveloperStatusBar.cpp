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
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
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

        // Shared chrome for the borderless status pills in the developer bar: elevated background,
        // status dot (haloed while busy) and the leading label. Returns where the pill content
        // (progress bar, thread dots, ...) starts, so callers only lay out what is theirs.
        struct FStatusPillMetrics
        {
            ImVec2 position;
            ImVec2 size;
            float contentStartX = 0.0f;
            float textY = 0.0f;
            float textSize = 0.0f;
            bool hovered = false;
        };

        constexpr float kStatusPillHeight = 22.0f;
        constexpr float kStatusPillRounding = 4.0f;
        constexpr float kStatusPillTextScale = 0.78f;
        constexpr float kStatusPillLabelX = 18.0f;
        constexpr float kStatusPillDotX = 10.0f;

        FStatusPillMetrics DrawStatusPillChrome(const char* label, float labelToContentGap, float contentWidth,
                                                float trailingWidth, NextUI::Foundation::EColor statusColor,
                                                bool busy)
        {
            ImFont* font = ImGui::GetFont();
            const float textSize = ImGui::GetFontSize() * kStatusPillTextScale;
            const ImVec2 labelSize = font->CalcTextSizeA(textSize, FLT_MAX, 0.0f, label);

            FStatusPillMetrics metrics;
            metrics.position = ImGui::GetCursorScreenPos();
            metrics.size = ImVec2(kStatusPillLabelX + labelSize.x + labelToContentGap + contentWidth + trailingWidth,
                                  kStatusPillHeight);
            metrics.contentStartX = metrics.position.x + kStatusPillLabelX + labelSize.x + labelToContentGap;
            metrics.textSize = textSize;
            metrics.textY = metrics.position.y + (kStatusPillHeight - textSize) * 0.5f;

            ImGui::Dummy(metrics.size);
            metrics.hovered = ImGui::IsItemHovered();

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(metrics.position, metrics.position + metrics.size,
                                    NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::SurfaceElevated,
                                                                 metrics.hovered ? 0.85f : 0.45f),
                                    kStatusPillRounding);

            const ImVec2 dotCenter(metrics.position.x + kStatusPillDotX,
                                   metrics.position.y + kStatusPillHeight * 0.5f);
            if (busy)
            {
                drawList->AddCircleFilled(dotCenter, 4.2f, NextUI::Foundation::ColorU32(statusColor, 0.25f));
            }
            drawList->AddCircleFilled(dotCenter, 2.5f, NextUI::Foundation::ColorU32(statusColor, 0.90f));

            drawList->AddText(font, textSize, ImVec2(metrics.position.x + kStatusPillLabelX, metrics.textY),
                              NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::TextMuted), label);
            return metrics;
        }

        void DrawBakeActivity(NextEngine& engine)
        {
            const auto DrawActivity = [](const char* label, const char* value, const float fraction,
                                         const bool indeterminate, const EBakeStatus status, const char* tooltip)
            {
                constexpr float progressWidth = 56.0f;
                constexpr float progressHeight = 3.5f;
                constexpr float contentGap = 7.0f;

                const NextUI::Foundation::EColor statusColor = status == EBakeStatus::Disabled
                    ? NextUI::Foundation::EColor::TextDim
                    : status == EBakeStatus::Complete
                        ? NextUI::Foundation::EColor::Success
                        : NextUI::Foundation::EColor::AccentHover;

                ImFont* font = ImGui::GetFont();
                const float probeSize = ImGui::GetFontSize() * kStatusPillTextScale;
                const float valueWidth = font->CalcTextSizeA(probeSize, FLT_MAX, 0.0f, value).x;
                const FStatusPillMetrics pill = DrawStatusPillChrome(
                    label, contentGap, progressWidth, contentGap + valueWidth + 8.0f, statusColor,
                    status == EBakeStatus::Progress);

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                const float textSize = pill.textSize;
                const float textY = pill.textY;
                const bool isHovered = pill.hovered;

                const ImVec2 progressMin(pill.contentStartX,
                                         pill.position.y + (kStatusPillHeight - progressHeight) * 0.5f);
                const ImVec2 progressMax(progressMin.x + progressWidth, progressMin.y + progressHeight);

                drawList->AddRectFilled(progressMin, progressMax,
                                        NextUI::Foundation::ColorU32(
                                            NextUI::Foundation::EColor::Background, 0.85f),
                                        progressHeight * 0.5f);

                if (status != EBakeStatus::Disabled && indeterminate)
                {
                    constexpr float segmentWidth = 18.0f;
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
                                            NextUI::Foundation::ColorU32(statusColor, status == EBakeStatus::Complete ? 0.70f : 0.95f),
                                            progressHeight * 0.5f);
                }

                drawList->AddText(font, textSize, ImVec2(progressMax.x + contentGap, textY),
                                  NextUI::Foundation::ColorU32(statusColor), value);

                if (isHovered && tooltip)
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

        void DrawTaskThreads()
        {
            const std::vector<Tasks::FTaskThreadStatus> statuses =
                Tasks::TaskCoordinator::GetInstance()->GetTaskThreadStatuses();
            constexpr float dotRadius = 2.5f;
            constexpr float dotGap = 3.5f;
            constexpr float contentGap = 8.0f;

            const float threadCount = static_cast<float>(statuses.size());
            const float dotsWidth = statuses.empty()
                ? 0.0f
                : threadCount * (dotRadius * 2.0f) + (threadCount - 1.0f) * dotGap;

            const bool hasActiveThread = std::any_of(
                statuses.begin(), statuses.end(), [](const Tasks::FTaskThreadStatus& thread)
                {
                    return thread.running || thread.queued > 0u;
                });
            const NextUI::Foundation::EColor statusColor = statuses.empty()
                ? NextUI::Foundation::EColor::TextDim
                : hasActiveThread
                    ? NextUI::Foundation::EColor::Warning
                    : NextUI::Foundation::EColor::Success;

            ImGui::SameLine(0.0f, 6.0f);
            const FStatusPillMetrics pill = DrawStatusPillChrome("Tasks", contentGap, dotsWidth, 8.0f,
                                                                 statusColor, hasActiveThread);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const bool isHovered = pill.hovered;
            const float dotsStartX = pill.contentStartX + dotRadius;
            const float dotsCenterY = pill.position.y + kStatusPillHeight * 0.5f;
            for (size_t index = 0; index < statuses.size(); ++index)
            {
                const Tasks::FTaskThreadStatus& thread = statuses[index];
                const bool active = thread.running || thread.queued > 0u;
                const float dotX = dotsStartX + static_cast<float>(index) * (dotRadius * 2.0f + dotGap);
                const ImVec2 threadDotCenter(dotX, dotsCenterY);

                if (active)
                {
                    // Active running/queued worker: energetic warning glow + full dot
                    drawList->AddCircleFilled(threadDotCenter, dotRadius + 1.2f,
                                              NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::Warning, 0.30f));
                    drawList->AddCircleFilled(threadDotCenter, dotRadius,
                                              NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::Warning, 0.95f));
                }
                else
                {
                    // Idle worker: calm, subtle translucent success dot with no border
                    drawList->AddCircleFilled(threadDotCenter, dotRadius,
                                              NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::Success, 0.40f));
                }
            }

            if (isHovered)
            {
                bool hitSpecificThread = false;
                for (size_t index = 0; index < statuses.size(); ++index)
                {
                    const float dotX = dotsStartX + static_cast<float>(index) * (dotRadius * 2.0f + dotGap);
                    const ImVec2 threadHitMin(dotX - dotRadius - 1.5f, dotsCenterY - dotRadius - 4.0f);
                    const ImVec2 threadHitMax(dotX + dotRadius + 1.5f, dotsCenterY + dotRadius + 4.0f);
                    if (ImGui::IsMouseHoveringRect(threadHitMin, threadHitMax))
                    {
                        const Tasks::FTaskThreadStatus& thread = statuses[index];
                        const char* state = thread.running
                            ? (thread.queued > 0u ? "Running + queued" : "Running")
                            : (thread.queued > 0u ? "Queued" : "Idle");
                        const std::string tooltip = fmt::format(
                            "{}\nStatus: {}\nQueued: {}", thread.name, state, thread.queued);
                        NextUI::Theme::DrawTooltip(tooltip.c_str());
                        hitSpecificThread = true;
                        break;
                    }
                }
                if (!hitSpecificThread)
                {
                    uint32_t activeCount = 0;
                    for (const auto& t : statuses)
                    {
                        if (t.running || t.queued > 0u) ++activeCount;
                    }
                    const std::string summaryTooltip = fmt::format(
                        "Task Thread Pool\nThreads: {} ({} active, {} idle)",
                        statuses.size(), activeCount, statuses.size() - activeCount);
                    NextUI::Theme::DrawTooltip(summaryTooltip.c_str());
                }
            }
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
        constexpr float minFpsWidth = 76.0f;
        const float fpsWidth = std::max(minFpsWidth, ImGui::CalcTextSize(fpsText.c_str()).x + 16.0f);
        constexpr float minMemoryWidth = 140.0f;
        const float memoryWidth = std::max(minMemoryWidth, ImGui::CalcTextSize(memoryText.c_str()).x + 16.0f);
        const float rightWidth = consoleWidth + toolWidth * (3.0f + (renderDocSupported ? 1.0f : 0.0f)) +
            fpsWidth + memoryWidth + 98.0f + (renderDocSupported ? 20.0f : 0.0f);

        NextUI::Foundation::FBottomBarOptions options;
        options.windowId = windowId;
        options.height = height;
        options.rightWidth = rightWidth;
        options.drawLeftContent = [&engine]()
        {
            DrawBakeActivity(engine);
            DrawTaskThreads();
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
            const std::string fpsButtonId = fmt::format("{}###BottomBarFps", fpsText);
            if (StatusButton(fpsButtonId.c_str(), "Toggle Stats Overlay (F3)", engine.GetUserSettings().ShowOverlay,
                             ImVec2(fpsWidth, buttonHeight)))
            {
                static uint64_t lastFpsToggleTimeMs = 0;
                const uint64_t now = SDL_GetTicks();
                if (now - lastFpsToggleTimeMs > 200)
                {
                    lastFpsToggleTimeMs = now;
                    engine.GetUserSettings().ShowOverlay = !engine.GetUserSettings().ShowOverlay;
                    engine.GetShowFlags().DebugProfileOverlay = engine.GetUserSettings().ShowOverlay;
                }
            }
            DrawSeparator();
            const std::string memoryButtonId = fmt::format("{}###BottomBarMemory", memoryText);
            if (StatusButton(memoryButtonId.c_str(), "Show VRAM details", panels.IsMemoryStatisticsOpen(),
                             ImVec2(memoryWidth, buttonHeight)))
            {
                panels.ToggleMemoryStatistics();
            }
        };
        NextUI::Foundation::DrawBottomBar(options);
    }
}
