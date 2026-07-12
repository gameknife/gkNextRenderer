#pragma once

#include "ScadPromptContext.hpp"
#include "ScadSession.hpp"

#include "Modules/NextAI/AI/AIChat.hpp"
#include "Modules/NextAI/AIService.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <thread>

class NextEngine;

namespace ScadStudio
{
    // Result of one generation turn, consumed on the main thread.
    struct FScadGenResult
    {
        bool success = false;
        std::string assistantText; // full model reply (for the chat bubble)
        std::string scadSource;    // extracted ```scad``` block (empty if none)
        std::vector<FScadProjectFile> files; // extracted ```scad-project``` files (empty for single-file replies)
        std::string error;         // populated when success == false
    };

    // Thin wrapper around the engine's NextAI::FAIService that:
    //  - builds a SCAD-specialised system prompt constraining output to the loader subset,
    //  - runs FAIService::Chat() on a background thread (the engine API is blocking),
    //  - marshals the result back to the main thread via an atomic/mutex hand-off
    //    (mirrors the MagicaLego / Editor AI service pattern).
    class ScadAIService
    {
    public:
        explicit ScadAIService(NextEngine& engine);
        ~ScadAIService();

        bool IsConfigured() const;
        bool IsGenerating() const { return generating_.load(); }
        std::string ProviderName() const;
        std::string ProviderId() const;
        std::vector<NextAI::FAIProviderDescriptor> Providers() const;
        bool IsProviderConfigured(const std::string& providerId) const;
        bool SwitchProvider(const std::string& providerId);
        std::vector<std::string> CurrentProviderModels() const;
        std::string CurrentModel() const;
        bool SetCurrentModel(const std::string& model);

        // Kick off a generation. `currentSource` is the authoritative model state fed
        // back so multi-turn edits ("make it taller") resolve correctly; pass empty for
        // a fresh model. Safe to call only when !IsGenerating().
        void SubmitAsync(
            const std::string& currentSource,
            const std::vector<FScadProjectFile>& files,
            const FScadEditScope& editScope,
            const std::string& instruction);

        bool HasPendingResult() const { return hasPending_.load(); }
        FScadGenResult TakePendingResult();
        std::string StreamingText() const;

        // Drop multi-turn history (e.g. when switching sessions).
        void ResetConversation();

    private:
        std::string BuildSystemPrompt() const;
        static std::string ExtractScadBlock(const std::string& text);
        static std::vector<FScadProjectFile> ExtractProjectFiles(const std::string& text);

        NextEngine& engine_;

        std::atomic<bool> generating_{false};
        std::atomic<bool> hasPending_{false};
        mutable std::mutex mutex_;
        FScadGenResult pending_;
        std::string streamingText_;
        std::jthread worker_;

        // Plain-text turn history (no embedded source); the live source is injected into
        // the latest user message at request-build time. Main-thread only.
        std::vector<NextAI::FChatMessage> conversation_;
        static constexpr size_t kMaxConversationMessages = 24;
    };
}
