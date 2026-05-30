#pragma once

#include "ScadAIService.hpp"
#include "ScadSession.hpp"
#include "ScadSessionStore.hpp"

#include <filesystem>
#include <string>
#include <vector>

class NextEngine;

namespace ScadStudio
{
    // Owns the three-pane UI (Sessions | Viewport | Chat), the AI service, the session
    // list, and disk persistence. Driven by ScadStudioGameInstance's UI lifecycle hooks.
    class ScadStudioInterface
    {
    public:
        explicit ScadStudioInterface(NextEngine& engine);

        void Config(); // OnPreConfigUI: ImGui config flags
        void Init();   // OnInitUI: fonts
        void Render(); // OnRenderUI: dockspace + panels + viewport mapping

    private:
        void BuildDockLayout(unsigned int dockId);
        void DrawSessionPanel();
        void DrawOutline(const std::vector<FOutlineNode>& nodes);
        void DrawChatPanel();
        void PollAI();
        void SubmitCurrentInput();
        FScadSession& NewSession();
        void DeleteSession(int index);
        void SelectSession(int index);
        void WriteAndReload(FScadSession& session);
        void RefreshOutline(FScadSession& session);
        void ExportSession(const FScadSession& session);
        void PersistSession(const FScadSession& session);

        NextEngine& engine_;
        ScadAIService ai_;
        ScadSessionStore store_;

        std::vector<FScadSession> sessions_;
        int current_ = -1;
        int sessionCounter_ = 0;

        // The session id that owns the in-flight AI request (so a result lands on the
        // right session even if the user switches selection mid-generation).
        std::string pendingSessionId_;
        // Remaining auto-repair attempts for the in-flight request.
        int repairBudget_ = 0;
        static constexpr int kMaxRepairAttempts = 2;
        bool autoRepair_ = true;

        char inputBuf_[8192] = {};
        char renameBuf_[256] = {};
        int renamingIndex_ = -1;
        bool firstRun_ = true;
        bool welcomeLoaded_ = false;
        bool scrollChatToBottom_ = false;
    };
}
