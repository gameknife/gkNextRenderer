#pragma once

#include "ScadAIController.hpp"

#include <functional>
#include <string>

namespace ScadLibrary::AI
{
    struct FScadAIPanelActions
    {
        std::function<void(const std::string&)> submit;
        std::function<void()> preview;
        std::function<void()> compareOriginal;
        std::function<void()> apply;
        std::function<void()> reject;
        std::function<void()> undo;
        std::function<void()> regenerate;
    };

    class FScadAIPanel
    {
    public:
        void Draw(const std::string& targetLabel, FScadAIController& controller, bool canApply,
                  bool canUndo, bool showingCandidate, const FScadAIPanelActions& actions);

    private:
        void RefreshConfiguration(FScadAIController& controller);

        char instruction_[2048] = {};
        FScadAITransportConfiguration configuration_;
        std::string configurationError_;
        bool configurationLoaded_ = false;
        bool configurationRequested_ = false;
    };
} // namespace ScadLibrary::AI
