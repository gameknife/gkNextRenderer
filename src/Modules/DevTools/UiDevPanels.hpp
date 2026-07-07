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

namespace NextUI
{
    struct Statistics;
}

namespace DevTools
{
    // Developer console + statistics overlay, extracted from the engine core
    // ImGui backend (NextUI::UserInterface). Driven per-frame through the
    // IDebugUiProvider hooks in DevToolsDebugUiProvider.
    class FUiDevPanels final
    {
    public:
        static FUiDevPanels& Get();

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
        void DrawOverlay(const NextUI::Statistics& statistics, Runtime::FrameProfiler* profiler);

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

        struct TimingSample
        {
            double sampleTime = 0.0;
            float milliseconds = 0.0f;
        };

        struct TimingHistory
        {
            std::deque<TimingSample> samples;
            std::string displayName;
            double lastSeenTime = 0.0;
            float average = 0.0f;
            float minimum = 0.0f;
            float maximum = 0.0f;
            int depth = 0;
            uint32_t displayOrder = 0;
        };

        std::vector<std::string> consoleHistory_;
        std::vector<std::string> consoleMatches_;
        std::string consoleInput_;
        std::string consoleLastInput_;
        std::string consoleCompletionBase_;
        int consoleHistoryIndex_ = -1;
        int consoleMatchSelection_ = -1;
        bool consoleSkipEditReset_ = false;
        bool showConsole_ = false;
        bool consoleInteractiveMode_ = false;
        bool consoleScrollToBottom_ = false;
        bool requestConsoleFocus_ = false;
        bool suppressConsoleToggleTextInput_ = false;
        uint64_t consoleLogRevision_ = 0;
        std::unordered_map<std::string, TimingHistory> gpuTimeHistory_;
        std::unordered_map<std::string, TimingHistory> cpuTimeHistory_;

        static constexpr int kOverlaySparklineSampleCount = 64;
        static constexpr int kOverlaySparklineSampleStride = 2;
        std::array<float, kOverlaySparklineSampleCount> frameRateSamples_{};
        std::array<float, kOverlaySparklineSampleCount> frameTimeSamples_{};
        int overlaySampleCursor_ = 0;
        int overlaySampleFilled_ = 0;
        int overlaySampleStrideCounter_ = 0;
    };
}
