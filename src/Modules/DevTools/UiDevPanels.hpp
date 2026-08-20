#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <imgui.h>

#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

class NextEngine;
union SDL_Event;

namespace NextCVar
{
    class FCVarSystem;
}

namespace NextUI
{
    struct Statistics;
}

namespace DevTools
{
    // Developer console + statistics overlay, extracted from the engine core
    // ImGui backend (NextUI::IUserInterface). Driven per-frame through the
    // IDebugUiProvider hooks in DevToolsDebugUiProvider.
    class FUiDevPanels final
    {
    public:
        static FUiDevPanels& Get();
        void RegisterCVars(NextCVar::FCVarSystem& cvars);

        // Console
        void SubmitConsoleCommand(const std::string& command);
        bool DrawConsoleCommandInput(const char* label, const char* hint, float width = 0.0f,
                                     bool closeConsoleOnSubmit = false, bool showMatchPopup = false,
                                     const char* matchPopupId = nullptr, bool refreshMatches = true);
        void DrawConsoleLogOutput(const char* childId, const ImVec2& size = ImVec2(0.0f, 0.0f), bool bordered = true);
        void ToggleConsole();
        bool IsConsoleOpen() const { return showConsole_; }
        void RenderConsoleOverlay();

        // Statistics overlay
        void DrawOverlay(const NextUI::Statistics& statistics);
        void SetStatisticsDetachedViewport(bool detached) { statisticsDetachedViewport_ = detached; }

        // Memory statistics
        void ToggleMemoryStatistics();
        void SetMemoryStatisticsOpen(bool open) { showMemoryStatistics_ = open; }
        bool IsMemoryStatisticsOpen() const { return showMemoryStatistics_; }
        void DrawMemoryStatisticsPanel(NextEngine& engine);

        // Grave-key console toggle; returns true when the event was consumed.
        bool HandleEvent(const SDL_Event& event);

    private:
        NextEngine& Engine();

        void DrawConsoleWindow();
        void RefreshConsoleMatches(size_t matchLimit);
        void DrawConsoleMatchPopup(float width, const char* popupId);
        static int ConsoleInputTextCallback(ImGuiInputTextCallbackData* data);
        int HandleConsoleInputTextCallback(ImGuiInputTextCallbackData* data);
        void DrawConsoleLogOutputInternal(const char* childId, const ImVec2& size, bool bordered);

        std::vector<std::string> consoleHistory_;
        std::vector<std::string> consoleMatches_;
        std::string consoleInput_;
        std::string consoleLastInput_;
        std::string consoleCompletionBase_;
        int consoleHistoryIndex_ = -1;
        int consoleMatchSelection_ = -1;
        bool consoleSkipEditReset_ = false;
        bool showConsole_ = false;
        bool showMemoryStatistics_ = false;
        bool statisticsDetachedViewport_ = false;
        bool consoleInteractiveMode_ = false;
        bool consoleScrollToBottom_ = false;
        bool requestConsoleFocus_ = false;
        bool suppressConsoleToggleTextInput_ = false;
        uint64_t consoleLogRevision_ = 0;
        static constexpr int kOverlaySparklineSampleCount = 64;
        static constexpr int kOverlaySparklineSampleStride = 2;
        std::array<float, kOverlaySparklineSampleCount> frameRateSamples_{};
        std::array<float, kOverlaySparklineSampleCount> frameTimeSamples_{};
        int overlaySampleCursor_ = 0;
        int overlaySampleFilled_ = 0;
        int overlaySampleStrideCounter_ = 0;
    };
}
