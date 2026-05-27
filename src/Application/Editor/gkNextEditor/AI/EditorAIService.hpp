#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "AI/EditorScriptExecutor.hpp"
#include "Engine/Runtime/Subsystems/AIService.hpp"
#include "Engine/Runtime/Subsystems/AI/IAITool.hpp"
#include "Engine/Runtime/Subsystems/AI/MainThreadDispatcher.hpp"
#include "Engine/Runtime/Subsystems/AI/ToolRegistry.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class NextEngine;
struct EditorContext;

namespace Editor
{
    enum class EEditorAIStatus
    {
        Idle,
        Generating,
        Executing,
        Error
    };

    struct FCodeBlock
    {
        enum class EType
        {
            EditorScript,
            JavaScript
        };
        EType type;
        std::string code;
    };

    struct FPendingEditorAction
    {
        uint64_t id = 0;
        FDeferredEditorAction request;
    };

    class FEditorAIService
    {
    public:
        explicit FEditorAIService(NextEngine& engine);
        ~FEditorAIService();

        // Build the legacy single-shot system prompt with scene context (still used when
        // useAgentLoop_=false).
        std::string BuildSystemPrompt(const EditorContext& ctx);

        // Build the lean agent-mode system prompt (tools-first; no scene context dump).
        std::string BuildAgentSystemPrompt(const EditorContext& ctx);

        // Send user prompt to AI asynchronously
        void GenerateAsync(const std::string& userPrompt, const EditorContext& ctx);

        // Called every frame by AIPanel so agent tools can access the current
        // EditorContext (which is stack-scoped per frame) and so the dispatcher
        // can drain queued main-thread tasks.
        void SetCurrentContext(EditorContext* ctx) { currentContext_ = ctx; }
        void PumpMainThread();

        // Poll for completion (call from main thread tick)
        bool HasPendingResult() const { return hasPendingResult_.load(); }

        // Consume pending result and execute it
        void ConsumePendingResult(EditorContext& ctx);

        // Direct execution of script text (for manual input)
        void ExecuteDirect(const std::string& input, EditorContext& ctx);

        EEditorAIStatus GetStatus() const { return status_; }
        const std::string& GetStatusMessage() const { return statusMessage_; }
        bool IsAIConfigured() const;

        const std::vector<FPendingEditorAction>& GetPendingActions() const { return pendingActions_; }
        bool ConfirmPendingAction(uint64_t actionId, EditorContext& ctx);
        bool CancelPendingAction(uint64_t actionId);

        // Get logs from the script executor
        std::vector<ScriptLogEntry> TakeLog();

        // Get the raw AI response for display
        const std::string& GetLastResponse() const { return lastResponse_; }

        // Agent loop integration (sink populated only when useAgentLoop_=true; see Phase 7).
        NextAI::FInMemoryAgentEventSink& GetAgentEventSink() { return agentEventSink_; }

        // Request cancellation of an in-flight Generate. The flag is consumed at the
        // next agent-loop step boundary or async checkpoint.
        void RequestCancel() { cancelRequested_.store(true); }
        bool IsCancelRequested() const { return cancelRequested_.load(); }

        // User-tunable config (loaded from ai_config.json fields useAgentLoop / maxAgentSteps).
        bool IsAgentLoopEnabled() const { return useAgentLoop_; }
        void SetAgentLoopEnabled(bool v) { useAgentLoop_ = v; }
        int GetMaxAgentSteps() const { return maxAgentSteps_; }
        void SetMaxAgentSteps(int n) { maxAgentSteps_ = (n < 1 ? 1 : (n > 30 ? 30 : n)); }

    private:
        void LoadAgentConfig();
        void EnsureToolRegistry();
        void RunAgentLoopAsync(const std::string& userPrompt, const EditorContext& ctx);
        void RunLegacyAsync(const std::string& userPrompt, const EditorContext& ctx);
        void HarvestDeferredActions();

        std::string BuildSceneContext(const EditorContext& ctx);
        std::string BuildSelectionContext(const EditorContext& ctx);
        std::string BuildSceneAssetCatalog();
        std::string BuildAvailableCommands();
        std::string BuildPropertyTypeInfo(const EditorContext& ctx);
        std::vector<FCodeBlock> ExtractFromResponse(const std::string& response);

        NextEngine& engine_;
        FEditorScriptExecutor executor_;

        EEditorAIStatus status_ = EEditorAIStatus::Idle;
        std::string statusMessage_;
        std::string lastResponse_;

        std::mutex resultMutex_;
        std::atomic<bool> hasPendingResult_{false};
        NextAI::FAIResponse pendingResponse_;

        std::vector<FPendingEditorAction> pendingActions_;
        uint64_t nextPendingActionId_ = 1;

        // Agent infrastructure.
        NextAI::FInMemoryAgentEventSink agentEventSink_;
        std::atomic<bool> cancelRequested_{false};
        bool useAgentLoop_ = true;
        int maxAgentSteps_ = 12;

        NextAI::FMainThreadDispatcher dispatcher_;
        NextAI::FToolRegistry toolRegistry_;
        bool toolRegistryInitialized_ = false;
        EditorContext* currentContext_ = nullptr;
    };
} // namespace Editor
