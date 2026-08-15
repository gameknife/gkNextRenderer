#include "Engine/Common/CoreMinimal.hpp"

#include "DeveloperStatusBar.hpp"

#include "Engine/Runtime/Editor/UI/AppChrome.hpp"
#include "Engine/Runtime/Editor/UI/UiScopes.hpp"
#include "Engine/Runtime/Editor/UI/UiTheme.hpp"
#include "Engine/Runtime/Editor/UI/UiWidgets.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Format.hpp"
#include "Modules/DevTools/UiDevPanels.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"

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
    }

    void DrawDeveloperStatusBar(NextEngine& engine,
                                const char* windowId,
                                const float height,
                                std::function<void()> onCppReloadClicked,
                                const bool cppLiveCodingAvailable,
                                const bool detachedStatisticsViewport,
                                std::function<bool()> drawActivityIndicator)
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
        const float fpsWidth = ImGui::CalcTextSize(fpsText.c_str()).x + 12.0f;
        const float memoryWidth = ImGui::CalcTextSize(memoryText.c_str()).x + 12.0f;
        const float rightWidth = consoleWidth + toolWidth * 2.0f + fpsWidth + memoryWidth + 98.0f;

        NextUI::Foundation::FBottomBarOptions options;
        options.windowId = windowId;
        options.height = height;
        options.rightWidth = rightWidth;
        options.drawLeftContent = [&drawActivityIndicator]()
        {
            if (drawActivityIndicator && drawActivityIndicator())
            {
                return;
            }
            const ImVec2 position = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(position.x + 4.0f, position.y + ImGui::GetTextLineHeight() * 0.5f), 3.5f,
                NextUI::Foundation::ColorU32(NextUI::Foundation::EColor::Success));
            ImGui::Dummy(ImVec2(11.0f, ImGui::GetTextLineHeight()));
            ImGui::SameLine(0.0f, 3.0f);
            ImGui::TextColored(
                NextUI::Foundation::Color(NextUI::Foundation::EColor::TextMuted), "Ready");
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
            if (StatusButton(ICON_FA_WAND_MAGIC_SPARKLES "##BottomBarRebuildShaders",
                             shaderLive ? "Rebuild shaders now (hot reload ready)" : "Rebuild shaders now",
                             shaderLive, ImVec2(toolWidth, buttonHeight)))
            {
                engine.RequestShaderHotReload();
            }
            ImGui::SameLine(0.0f, 4.0f);
            {
                NextUI::Foundation::FScopedDisabled disabled(!cppLiveCodingAvailable || !onCppReloadClicked);
                if (StatusButton(ICON_FA_HAMMER "##BottomBarCompileCpp",
                                 cppLiveCodingAvailable ? "Compile C++ changes" : "Live++ is unavailable",
                                 cppLiveCodingAvailable, ImVec2(toolWidth, buttonHeight)) && onCppReloadClicked)
                {
                    onCppReloadClicked();
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
