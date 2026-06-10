#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/DevTools/UiDevPanels.hpp"
#include "Modules/DevTools/ConsoleLogBuffer.hpp"
#include "Modules/DevTools/ProfessionalUI.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Engine/Utilities/Math.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

#include <imgui.h>
#include <imgui_stdlib.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace DevTools
{
namespace
{
    std::string ExtractConsolePrefix(const std::string& input)
    {
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return {};
        }
        size_t end = input.find_first_of(" =\t\r\n", start);
        if (end == std::string::npos)
        {
            return input.substr(start);
        }
        return input.substr(start, end - start);
    }

    std::string GetPhysicalDeviceDriverName(VkPhysicalDevice physicalDevice)
    {
        if (physicalDevice == VK_NULL_HANDLE)
        {
            return {};
        }

        VkPhysicalDeviceDriverProperties driverProperties{};
        driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

        VkPhysicalDeviceProperties2 deviceProperties{};
        deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties.pNext = &driverProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties);

        return driverProperties.driverName[0] != '\0' ? std::string(driverProperties.driverName) : std::string{};
    }
}

FUiDevPanels& FUiDevPanels::Get()
{
    static FUiDevPanels panels;
    return panels;
}

NextEngine& FUiDevPanels::Engine()
{
    return *NextEngine::GetInstance();
}

void FUiDevPanels::SubmitConsoleCommand(const std::string& command)
{
    if (command.empty())
    {
        return;
    }

    spdlog::info("> {}", command);

    consoleHistory_.push_back(command);
    constexpr size_t kConsoleHistoryLimit = 128;
    if (consoleHistory_.size() > kConsoleHistoryLimit)
    {
        consoleHistory_.erase(consoleHistory_.begin(),
                              consoleHistory_.begin() + static_cast<std::ptrdiff_t>(consoleHistory_.size() - kConsoleHistoryLimit));
    }
    consoleHistoryIndex_ = static_cast<int>(consoleHistory_.size());

    const auto result = Engine().GetCVarSystem().ExecuteCommand(command);
    if (!result.message.empty())
    {
        if (!result.success)
        {
            spdlog::error("{}", result.message);
        }
        else
        {
            spdlog::info("{}", result.message);
        }
    }

    for (const auto& line : result.output)
    {
        spdlog::info("  {}", line);
    }

    consoleScrollToBottom_ = true;
}

void FUiDevPanels::RefreshConsoleMatches(size_t matchLimit)
{
    if (consoleInput_ != consoleLastInput_)
    {
        consoleLastInput_ = consoleInput_;
        consoleMatchIndex_ = 0;
        consoleCompletionBase_.clear();
        consoleHistoryIndex_ = static_cast<int>(consoleHistory_.size());
    }

    std::string matchBase = consoleCompletionBase_.empty() ? ExtractConsolePrefix(consoleInput_) : consoleCompletionBase_;
    if (!matchBase.empty())
    {
        consoleMatches_ = Engine().GetCVarSystem().Match(matchBase, {.limit = matchLimit});
    }
    else
    {
        consoleMatches_.clear();
    }
}

void FUiDevPanels::DrawConsoleMatchPopup(float width, const char* popupId)
{
    if (popupId == nullptr || consoleMatches_.empty() || !ImGui::IsItemActive())
    {
        return;
    }

    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const float itemWidth = (width > 0.0f) ? width : (itemMax.x - itemMin.x);
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float popupHeight = std::min(rowHeight * (static_cast<float>(consoleMatches_.size()) + 1.5f), rowHeight * 9.0f);
    const float offset = 2.0f;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float viewportBottom = viewport->Pos.y + viewport->Size.y;
    const float yBelow = itemMax.y + offset;
    const float yAbove = itemMin.y - popupHeight - offset;
    const float popupY = (yBelow + popupHeight <= viewportBottom) ? yBelow : std::max(viewport->Pos.y + offset, yAbove);

    ImGui::SetNextWindowPos(ImVec2(itemMin.x, popupY));
    ImGui::SetNextWindowSize(ImVec2(itemWidth, popupHeight));
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin(popupId, nullptr, popupFlags))
    {
        ImGui::Text("Matches:");
        ImGui::Separator();
        for (const auto& name : consoleMatches_)
        {
            ImGui::TextUnformatted(name.c_str());
        }
    }
    ImGui::End();
}

bool FUiDevPanels::DrawConsoleCommandInput(
    const char* label, const char* hint, float width, bool closeConsoleOnSubmit, bool showMatchPopup,
    const char* matchPopupId, bool refreshMatches)
{
    constexpr size_t kMatchLimit = 8;
    if (refreshMatches)
    {
        RefreshConsoleMatches(kMatchLimit);
    }

    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
        ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackEdit;
    if (width > 0.0f)
    {
        ImGui::SetNextItemWidth(width);
    }

    const bool executeCommand =
        ImGui::InputTextWithHint(label, hint, &consoleInput_, inputFlags, &FUiDevPanels::ConsoleInputTextCallback, this);
    if (showMatchPopup)
    {
        DrawConsoleMatchPopup(width, matchPopupId);
    }

    if (!executeCommand || consoleInput_.empty())
    {
        return false;
    }

    SubmitConsoleCommand(consoleInput_);
    consoleInput_.clear();
    consoleLastInput_.clear();
    consoleMatches_.clear();
    consoleCompletionBase_.clear();
    consoleMatchIndex_ = 0;
    if (closeConsoleOnSubmit)
    {
        showConsole_ = false;
        requestConsoleFocus_ = false;
    }
    return true;
}

int FUiDevPanels::ConsoleInputTextCallback(ImGuiInputTextCallbackData* data)
{
    auto* ui = static_cast<FUiDevPanels*>(data->UserData);
    if (ui == nullptr)
    {
        return 0;
    }
    return ui->HandleConsoleInputTextCallback(data);
}

int FUiDevPanels::HandleConsoleInputTextCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
    {
        if (consoleSkipEditReset_)
        {
            consoleSkipEditReset_ = false;
            return 0;
        }
        consoleCompletionBase_.clear();
        consoleMatchIndex_ = 0;
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    {
        if (data->EventKey == ImGuiKey_UpArrow)
        {
            if (consoleHistoryIndex_ > 0)
            {
                consoleHistoryIndex_--;
            }
            else if (!consoleHistory_.empty())
            {
                consoleHistoryIndex_ = 0;
            }
        }
        else if (data->EventKey == ImGuiKey_DownArrow)
        {
            if (consoleHistoryIndex_ + 1 < static_cast<int>(consoleHistory_.size()))
            {
                consoleHistoryIndex_++;
            }
            else
            {
                consoleHistoryIndex_ = static_cast<int>(consoleHistory_.size());
            }
        }

        std::string historyCmd;
        if (!consoleHistory_.empty() && consoleHistoryIndex_ >= 0 &&
            consoleHistoryIndex_ < static_cast<int>(consoleHistory_.size()))
        {
            historyCmd = consoleHistory_[consoleHistoryIndex_];
        }

        data->DeleteChars(0, data->BufTextLen);
        if (!historyCmd.empty())
        {
            data->InsertChars(0, historyCmd.c_str());
        }
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
    {
        std::string buffer(data->Buf, data->BufTextLen);
        size_t start = buffer.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return 0;
        }
        size_t end = buffer.find_first_of(" =\t\r\n", start);
        if (end == std::string::npos)
        {
            end = buffer.size();
        }
        if (data->CursorPos > static_cast<int>(end))
        {
            return 0;
        }

        std::string prefix = buffer.substr(start, end - start);
        if (prefix.empty())
        {
            return 0;
        }

        if (consoleCompletionBase_.empty())
        {
            consoleCompletionBase_ = prefix;
            consoleMatchIndex_ = 0;
        }

        constexpr size_t kMatchLimit = 8;
        auto matches = Engine().GetCVarSystem().Match(consoleCompletionBase_, {.limit = kMatchLimit});
        consoleMatches_ = matches;
        if (matches.empty())
        {
            return 0;
        }

        int index = consoleMatchIndex_ % static_cast<int>(matches.size());
        const std::string& match = matches[index];
        consoleMatchIndex_ = (index + 1) % static_cast<int>(matches.size());

        std::string rest = end < buffer.size() ? buffer.substr(end) : "";
        std::string newBuffer = match + rest;

        consoleSkipEditReset_ = true;
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, newBuffer.c_str());
        data->CursorPos = static_cast<int>(match.size());
    }

    return 0;
}

void FUiDevPanels::DrawConsoleLogOutput(const char* childId, const ImVec2& size, bool bordered)
{
    DrawConsoleLogOutputInternal(childId, size, bordered);
}

void FUiDevPanels::DrawConsoleLogOutputInternal(const char* childId, const ImVec2& size, bool bordered)
{
    const auto logSink = Runtime::Editor::GetConsoleLogSink();
    const std::vector<spdlog::details::log_msg_buffer> lines = logSink ? logSink->last_raw() : std::vector<spdlog::details::log_msg_buffer>{};
    const uint64_t revision = Runtime::Editor::GetConsoleLogSequence();
    static ImGuiTextFilter consoleFilter;
    static bool showInfo = true;
    static bool showWarn = true;
    static bool showError = true;
    static bool showDebug = true;
    static size_t clearedLineOffset = 0;

    if (clearedLineOffset > lines.size())
    {
        clearedLineOffset = 0;
    }

    auto LevelInfo = [](spdlog::level::level_enum level) -> std::pair<const char*, ImVec4>
    {
        switch (level)
        {
        case spdlog::level::trace:
        case spdlog::level::debug:
            return {"[Debug]", ImVec4(0.55f, 0.9f, 0.95f, 1.0f)};
        case spdlog::level::info:
            return {"[Info]", ImVec4(0.76f, 0.86f, 1.0f, 1.0f)};
        case spdlog::level::warn:
            return {"[Warn]", ImVec4(1.0f, 0.82f, 0.35f, 1.0f)};
        case spdlog::level::err:
        case spdlog::level::critical:
            return {"[Error]", ImVec4(1.0f, 0.45f, 0.45f, 1.0f)};
        case spdlog::level::off:
        case spdlog::level::n_levels:
            return {"[Info]", ImVec4(0.78f, 0.78f, 0.78f, 1.0f)};
        }
        return {"[Info]", ImVec4(0.78f, 0.78f, 0.78f, 1.0f)};
    };

    auto ShouldShowLevel = [&](spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::trace:
        case spdlog::level::debug:
            return showDebug;
        case spdlog::level::info:
            return showInfo;
        case spdlog::level::warn:
            return showWarn;
        case spdlog::level::err:
        case spdlog::level::critical:
            return showError;
        case spdlog::level::off:
        case spdlog::level::n_levels:
            return showInfo;
        }
        return true;
    };

    if (ImGui::Button("Clear"))
    {
        clearedLineOffset = lines.size();
        consoleScrollToBottom_ = true;
    }
    ImGui::SameLine();
    consoleFilter.Draw("Filter##ConsoleFilter", 220.0f);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warn", &showWarn);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &showError);
    ImGui::SameLine();
    ImGui::Checkbox("Debug", &showDebug);

    std::vector<size_t> visibleLines;
    visibleLines.reserve(lines.size());
    for (size_t i = clearedLineOffset; i < lines.size(); ++i)
    {
        const auto& line = lines[i];
        const std::string payload(line.payload.data(), line.payload.size());
        if (!ShouldShowLevel(line.level))
        {
            continue;
        }
        if (consoleFilter.IsActive() && !consoleFilter.PassFilter(payload.c_str()))
        {
            continue;
        }
        visibleLines.push_back(i);
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    const ImGuiChildFlags childFlags = bordered ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
    if (ImGui::BeginChild(childId, size, childFlags))
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visibleLines.size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& line = lines[visibleLines[static_cast<size_t>(i)]];
                const auto [prefix, prefixColor] = LevelInfo(line.level);

                const char* payloadStart = line.payload.data();
                const char* payloadEnd = payloadStart + line.payload.size();
                ImGui::PushStyleColor(ImGuiCol_Text, prefixColor);
                ImGui::TextUnformatted(prefix);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Text));
                ImGui::TextUnformatted(payloadStart, payloadEnd);
                ImGui::PopStyleColor();
            }
        }

        if (consoleScrollToBottom_ || revision != consoleLogRevision_)
        {
            ImGui::SetScrollHereY(1.0f);
            consoleScrollToBottom_ = false;
            consoleLogRevision_ = revision;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void FUiDevPanels::ToggleConsole()
{
    showConsole_ = !showConsole_;
    requestConsoleFocus_ = showConsole_;
}

void FUiDevPanels::RenderConsoleOverlay()
{
    DrawConsoleWindow();
}

void FUiDevPanels::DrawOverlay(const NextUI::Statistics& statistics, VulkanGpuTimer* gpuTimer)
{
    if (!Engine().GetUserSettings().ShowOverlay)
    {
        return;
    }

    if (overlaySampleStrideCounter_ == 0)
    {
        frameRateSamples_[overlaySampleCursor_] = statistics.FrameRate;
        frameTimeSamples_[overlaySampleCursor_] = statistics.FrameTime;
        overlaySampleCursor_ = (overlaySampleCursor_ + 1) % kOverlaySparklineSampleCount;
        overlaySampleFilled_ = std::min(overlaySampleFilled_ + 1, kOverlaySparklineSampleCount);
    }
    overlaySampleStrideCounter_ = (overlaySampleStrideCounter_ + 1) % kOverlaySparklineSampleStride;

    const auto& io = ImGui::GetIO();
    constexpr float distance = 12.0f;
    constexpr float panelWidth = 380.0f;
    const ImVec2 pos = ImVec2(io.DisplaySize.x - distance - panelWidth, distance + 44.0f);
    const float panelHeight = std::max(420.0f, io.DisplaySize.y - pos.y - 42.0f);

    if (!NextUI::Theme::BeginFloatingPanel(
            "##ProfilerPanel", ICON_FA_CHART_LINE, "Profiler", &Engine().GetUserSettings().ShowOverlay,
            pos, ImVec2(panelWidth, panelHeight)))
    {
        return;
    }

    ImGui::BeginChild("##ProfilerBody", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);

    constexpr float cardHorizontalInset = 4.0f;
    auto BeginCard = [&](const char* id, float height, ImGuiWindowFlags extraFlags = 0)
    {
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        const float cardWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - cardHorizontalInset * 2.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cardHorizontalInset);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceElevated, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.84f));
        ImGui::BeginChild(id, ImVec2(cardWidth, height), true, extraFlags);
    };

    auto EndCard = [&]()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    };

    auto BuildOrdered = [&](const std::array<float, kOverlaySparklineSampleCount>& src,
                            std::array<float, kOverlaySparklineSampleCount>& dst,
                            int& outCount)
    {
        outCount = overlaySampleFilled_;
        if (overlaySampleFilled_ < kOverlaySparklineSampleCount)
        {
            for (int i = 0; i < outCount; ++i)
            {
                dst[i] = src[i];
            }
        }
        else
        {
            for (int i = 0; i < kOverlaySparklineSampleCount; ++i)
            {
                dst[i] = src[(overlaySampleCursor_ + i) % kOverlaySparklineSampleCount];
            }
        }
    };

    std::array<float, kOverlaySparklineSampleCount> orderedFps{};
    std::array<float, kOverlaySparklineSampleCount> orderedFt{};
    int orderedCount = 0;
    BuildOrdered(frameRateSamples_, orderedFps, orderedCount);
    BuildOrdered(frameTimeSamples_, orderedFt, orderedCount);

    const ImVec4 colHeader = NextUI::Theme::Color(NextUI::Theme::EColor::Blue);
    const ImVec4 colLabel = NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted);
    const ImVec4 colVal = NextUI::Theme::Color(NextUI::Theme::EColor::Text);
    const ImVec4 colGood = NextUI::Theme::Color(NextUI::Theme::EColor::Success);
    const ImVec4 colWarn = NextUI::Theme::Color(NextUI::Theme::EColor::Warning);
    const ImVec4 colBad = NextUI::Theme::Color(NextUI::Theme::EColor::Danger);

    auto LabelVal = [&](const char* label, const char* fmt, auto... args)
    {
        ImGui::TextColored(colLabel, "%s", label);
        ImGui::SameLine(132.0f);
        ImGui::TextColored(colVal, fmt, args...);
    };

    {
        const Vulkan::Device& device = NextEngine::GetInstance()->GetRenderer().Device();
        const VkPhysicalDeviceProperties deviceProperties = device.DeviceProperties();
        const std::string driverName = GetPhysicalDeviceDriverName(device.PhysicalDevice());

        const ImVec4 fpsColor = statistics.FrameRate > 55.0f ? colGood
            : (statistics.FrameRate > 30.0f ? colWarn : colBad);
        const std::string fpsText = fmt::format("{:.0f}  FPS", statistics.FrameRate);
        const std::string ftText = fmt::format("{:.2f}  ms", statistics.FrameTime);

        BeginCard("##ProfilerDeviceCard", 180.0f);
        if (ImGui::BeginTable("##ProfilerDeviceHeader", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextColumn();
            ImGui::TextColored(colHeader, "Device");
            ImGui::TableNextColumn();
            ImGui::TextColored(colLabel, "Resolution");
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::TextColored(colVal, "%ux%u", statistics.FramebufferSize.width,
                               statistics.FramebufferSize.height);
            ImGui::EndTable();
        }
        ImGui::TextColored(colVal, "%s", deviceProperties.deviceName);
        if (!driverName.empty())
        {
            const float driverFontSize = ImGui::GetFontSize() * 0.84f;
            const ImVec4 driverColor = NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted, 0.72f);
            const ImVec2 driverPos = ImGui::GetCursorScreenPos();
            ImFont* font = ImGui::GetFont();
            const ImVec2 driverSize = font->CalcTextSizeA(driverFontSize, FLT_MAX, 0.0f, driverName.c_str());
            ImGui::GetWindowDrawList()->AddText(font, driverFontSize, driverPos,
                                                ImGui::GetColorU32(driverColor), driverName.c_str());
            ImGui::Dummy(ImVec2(driverSize.x, driverSize.y + 2.0f));
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        if (ImGui::BeginTable("##ProfilerSparklineTable", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableSetupColumn("Frame Rate", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Frame Time", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(colHeader, "Frame Rate");
            ImGui::TextColored(fpsColor, "%s", fpsText.c_str());
            NextUI::Theme::Sparkline(orderedFps.data(), orderedCount,
                                     ImVec2(ImGui::GetContentRegionAvail().x, 26.0f), colGood);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(colHeader, "Frame Time");
            ImGui::TextColored(colVal, "%s", ftText.c_str());
            NextUI::Theme::Sparkline(orderedFt.data(), orderedCount,
                                     ImVec2(ImGui::GetContentRegionAvail().x, 26.0f),
                                     NextUI::Theme::Color(NextUI::Theme::EColor::Blue));
            ImGui::EndTable();
        }
        EndCard();
    }

    auto& gpuDrivenStat = NextEngine::GetInstance()->GetScene().GetGpuDrivenStat();
    const auto& shadowGpuDrivenStats = NextEngine::GetInstance()->GetScene().GetShadowGpuDrivenStats();
    const uint32_t instanceCount = gpuDrivenStat.ProcessedCount > gpuDrivenStat.CulledCount
        ? gpuDrivenStat.ProcessedCount - gpuDrivenStat.CulledCount
        : 0;
    const uint32_t triangleCount = gpuDrivenStat.TriangleCount > gpuDrivenStat.CulledTriangleCount
        ? gpuDrivenStat.TriangleCount - gpuDrivenStat.CulledTriangleCount
        : 0;
    const uint32_t mainTasks = Tasks::TaskCoordinator::GetInstance()->GetMainTaskCount();
    const uint32_t lowTasks = Tasks::TaskCoordinator::GetInstance()->GetParralledTaskCount();
    const uint32_t completeTasks = Tasks::TaskCoordinator::GetInstance()->GetComleteTaskQueueCount();

    auto FormatVisibleOverTotal = [](uint32_t visibleCount, uint32_t totalCount)
    {
        return fmt::format("{} / {}",
                           Utilities::metricFormatter(static_cast<double>(visibleCount), ""),
                           Utilities::metricFormatter(static_cast<double>(totalCount), ""));
    };

    BeginCard("##ProfilerSceneStatsCard", 308.0f);
    ImGui::TextColored(colHeader, "Scene Stats");
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    std::vector<std::pair<std::string, std::string>> sceneStats = {
        {"Nodes", Utilities::metricFormatter(static_cast<double>(statistics.NodeCount), "")},
        {"Instances", Utilities::metricFormatter(static_cast<double>(statistics.InstanceCount), "")},
        {"Textures", std::to_string(statistics.TextureCount)},
        {"Draws", FormatVisibleOverTotal(instanceCount, gpuDrivenStat.ProcessedCount)},
        {"Triangles", FormatVisibleOverTotal(triangleCount, gpuDrivenStat.TriangleCount)},
        {"Tasks", fmt::format("{} / {} / {}", mainTasks, lowTasks, completeTasks)},
    };
    if ((sceneStats.size() & 1u) != 0u)
    {
        sceneStats.emplace_back("", "");
    }
    if (ImGui::BeginTable("##SceneStatsTable", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);
        auto DrawSceneStat = [&](const std::pair<std::string, std::string>& stat)
        {
            if (!stat.first.empty())
            {
                ImGui::TextColored(colLabel, "%s", stat.first.c_str());
                ImGui::TextColored(colVal, "%s", stat.second.c_str());
            }
        };
        for (size_t statIndex = 0; statIndex < sceneStats.size(); statIndex += 2)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawSceneStat(sceneStats[statIndex]);
            ImGui::TableSetColumnIndex(1);
            DrawSceneStat(sceneStats[statIndex + 1]);
        }
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextColored(colHeader, "Shadow Cascades");
    if (ImGui::BeginTable("##SceneShadowCascadeStatsTable", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Cascade", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn("Tri", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableHeadersRow();

        for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
        {
            const auto& stat = shadowGpuDrivenStats[cascade];
            const uint32_t shadowDrawCount = stat.ProcessedCount > stat.CulledCount
                ? stat.ProcessedCount - stat.CulledCount
                : 0;
            const uint32_t shadowTriangleCount = stat.TriangleCount > stat.CulledTriangleCount
                ? stat.TriangleCount - stat.CulledTriangleCount
                : 0;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(colLabel, "C%u", cascade);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(colVal, "%s",
                               FormatVisibleOverTotal(shadowDrawCount, stat.ProcessedCount).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(colVal, "%s",
                               FormatVisibleOverTotal(shadowTriangleCount, stat.TriangleCount).c_str());
        }
        ImGui::EndTable();
    }
    EndCard();

    struct TimingRow
    {
        std::string name;
        int depth = 0;
        float average = 0.0f;
        float minimum = 0.0f;
        float maximum = 0.0f;
        uint32_t displayOrder = 0;
        bool active = true;
    };

    constexpr double timingHistoryWindowSeconds = 2.0;
    constexpr double timingStaleSeconds = 3.0;
    const double now = ImGui::GetTime();

    auto BuildTimingRows = [&](const std::vector<VulkanGpuTimer::TimerStat>& times,
                               std::unordered_map<std::string, TimingHistory>& historyMap)
    {
        uint32_t currentDisplayOrder = 0;
        for (const auto& time : times)
        {
            const std::string& historyKey = time.stableKey;
            auto historyIter = historyMap.try_emplace(historyKey).first;
            auto& history = historyIter->second;

            // Keep stale timers in their previous slot so transient timers do not
            // cause the rest of the table to jump every frame. Existing rows only
            // move when the current traversal order would otherwise place them
            // above a timer we have already emitted this frame.
            if (history.displayOrder < currentDisplayOrder)
            {
                history.displayOrder = currentDisplayOrder;
            }
            currentDisplayOrder = history.displayOrder + 1;
            history.displayName = time.name;
            history.depth = time.depth;
            history.lastSeenTime = now;
            history.samples.push_back({now, time.milliseconds});

            while (!history.samples.empty() &&
                   now - history.samples.front().sampleTime > timingHistoryWindowSeconds)
            {
                history.samples.pop_front();
            }

            float sum = 0.0f;
            float minimum = 1000000.0f;
            float maximum = 0.0f;
            for (const auto& sample : history.samples)
            {
                sum += sample.milliseconds;
                minimum = std::min(minimum, sample.milliseconds);
                maximum = std::max(maximum, sample.milliseconds);
            }

            history.average = history.samples.empty() ? time.milliseconds : sum / static_cast<float>(history.samples.size());
            history.minimum = minimum;
            history.maximum = maximum;
        }

        for (auto iter = historyMap.begin(); iter != historyMap.end();)
        {
            auto& history = iter->second;
            while (!history.samples.empty() &&
                   now - history.samples.front().sampleTime > timingHistoryWindowSeconds)
            {
                history.samples.pop_front();
            }

            if (now - iter->second.lastSeenTime > timingStaleSeconds)
            {
                iter = historyMap.erase(iter);
            }
            else
            {
                ++iter;
            }
        }

        std::vector<TimingRow> timingRows;
        timingRows.reserve(historyMap.size());
        for (const auto& [key, history] : historyMap)
        {
            timingRows.push_back({history.displayName,
                                  history.depth,
                                  history.average,
                                  history.minimum,
                                  history.maximum,
                                  history.displayOrder,
                                  now - history.lastSeenTime <= 0.1});
        }
        std::sort(timingRows.begin(), timingRows.end(), [](const TimingRow& lhs, const TimingRow& rhs)
        {
            if (lhs.displayOrder != rhs.displayOrder)
            {
                return lhs.displayOrder < rhs.displayOrder;
            }
            if (lhs.active != rhs.active)
            {
                return lhs.active;
            }
            return lhs.name < rhs.name;
        });
        return timingRows;
    };

    auto DrawTimingSection = [&](const char* label, const char* tableId, const std::vector<TimingRow>& timingRows)
    {
        float totalTime = 0.0f;
        for (const auto& row : timingRows)
        {
            if (row.depth == 0)
            {
                totalTime += row.average;
            }
        }

        ImGui::TextColored(colHeader, "%s (avg %.2fms / %.1fs)", label, totalTime, timingHistoryWindowSeconds);

        auto TimingBarColor = [&](float milliseconds)
        {
            if (milliseconds < 1.0f)
            {
                return colGood;
            }
            if (milliseconds < 4.0f)
            {
                return colWarn;
            }
            return colBad;
        };

        if (ImGui::BeginTable(tableId, 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableHeadersRow();

            for (const auto& row : timingRows)
            {
                const float ratio = totalTime > 0.001f ? row.average / totalTime : 0.0f;
                const ImVec4 rowColor = row.depth == 0 ? colVal : colLabel;

                ImGui::TableNextRow();
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, row.active ? 1.0f : 0.45f);
                ImGui::TableNextColumn();
                ImGui::Indent(static_cast<float>(row.depth) * 12.0f);
                ImGui::TextColored(rowColor, "%s", row.name.c_str());
                ImGui::Unindent(static_cast<float>(row.depth) * 12.0f);

                ImGui::TableNextColumn();
                ImGui::TextColored(rowColor, "%.2f", row.average);
                ImGui::TableNextColumn();
                ImGui::TextColored(colLabel, "%.2f", row.minimum);
                ImGui::TableNextColumn();
                ImGui::TextColored(colLabel, "%.2f", row.maximum);
                ImGui::TableNextColumn();
                NextUI::Theme::DrawProgressBar(std::min(ratio, 1.0f),
                                                  TimingBarColor(row.average),
                                                  ImVec2(70.0f, ImGui::GetTextLineHeight()));
                ImGui::PopStyleVar();
            }
            ImGui::EndTable();
        }
    };

    const float timingCardHeight = std::max(180.0f, ImGui::GetContentRegionAvail().y - 42.0f);
    BeginCard("##ProfilerTimingCard", timingCardHeight, ImGuiWindowFlags_HorizontalScrollbar);
    if (gpuTimer)
    {
        const auto gpuTimingRows = BuildTimingRows(gpuTimer->FetchAllTimes(4), gpuTimeHistory_);
        DrawTimingSection("Pass Timing", "##GpuTimeTable", gpuTimingRows);

        const auto cpuTimingRows = BuildTimingRows(gpuTimer->FetchAllCpuTimes(5), cpuTimeHistory_);
        if (!cpuTimingRows.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            NextUI::Theme::DrawThinSeparator(0.55f);
            DrawTimingSection("CPU Time", "##CpuTimeTable", cpuTimingRows);
        }
    }
    else
    {
        ImGui::TextColored(colLabel, "Timing data is unavailable.");
    }
    EndCard();

    ImGui::EndChild();
    NextUI::Theme::EndFloatingPanel();
}

bool FUiDevPanels::HandleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat == 0 &&
        (event.key.key == SDLK_GRAVE || event.key.scancode == SDL_SCANCODE_GRAVE))
    {
        ToggleConsole();
        suppressConsoleToggleTextInput_ = true;
        return true;
    }

    if (suppressConsoleToggleTextInput_ && event.type == SDL_EVENT_TEXT_INPUT)
    {
        const bool isConsoleToggleText =
            std::strcmp(event.text.text, "`") == 0 || std::strcmp(event.text.text, "~") == 0;
        suppressConsoleToggleTextInput_ = false;
        if (isConsoleToggleText)
        {
            return true;
        }
    }

    return false;
}

void FUiDevPanels::DrawConsoleWindow()
{
    if (!showConsole_)
    {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return;
    }

    constexpr float kBottomBarReservedHeight = 34.0f;
    const ImVec2 windowPos = viewport->Pos;
    const ImVec2 windowSize(viewport->Size.x, std::max(140.0f, viewport->Size.y - kBottomBarReservedHeight));

    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0.7f);

    const auto flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Console", &showConsole_, flags))
    {
        const size_t kConsoleMatchLimit = 8;
        RefreshConsoleMatches(kConsoleMatchLimit);

        float hintHeight = 0.0f;
        if (!consoleMatches_.empty())
        {
            hintHeight = ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(consoleMatches_.size()) + 1.0f);
        }

        float inputHeight = ImGui::GetFrameHeightWithSpacing();
        float outputHeight = ImGui::GetContentRegionAvail().y - inputHeight - hintHeight;
        outputHeight = std::max(outputHeight, ImGui::GetFontSize() * 5.0f);

        DrawConsoleLogOutputInternal("ConsoleOutput", ImVec2(0, outputHeight), true);

        if (!consoleMatches_.empty())
        {
            ImGui::BeginChild("ConsoleMatches", ImVec2(0, hintHeight), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("Matches (%zu/%zu):", consoleMatches_.size(), kConsoleMatchLimit);
            ImGui::PopStyleColor();
            for (const auto& name : consoleMatches_)
            {
                ImGui::Text("%s", name.c_str());
            }
            ImGui::EndChild();
        }

        if (requestConsoleFocus_)
        {
            ImGui::SetKeyboardFocusHere();
            requestConsoleFocus_ = false;
        }

        ImGui::PushItemWidth(-1);
        DrawConsoleCommandInput("##ConsoleInput", "", -1.0f, true, false, nullptr, false);
        ImGui::PopItemWidth();
    }
    ImGui::End();
}

}
