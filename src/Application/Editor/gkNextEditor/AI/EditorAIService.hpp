#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "AI/EditorScriptExecutor.hpp"
#include "Modules/NextAI/AIService.hpp"
#include "Modules/NextAI/AI/IAITool.hpp"
#include "Modules/NextAI/AI/ToolRegistry.hpp"
#include "Modules/NextAI/GnbClient/GnbAgentClient.hpp"

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

	struct FGnbProviderOption
	{
		std::string id;
		std::string displayName;
		std::string defaultModel;
		bool configured = false;
	};

    class FEditorAIService
    {
    public:
        explicit FEditorAIService(NextEngine& engine);
        ~FEditorAIService();

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

        // Reset the multi-turn conversation history (e.g. a "New Chat" button).
        void ClearConversation();

        EEditorAIStatus GetStatus() const { return status_; }
        const std::string& GetStatusMessage() const { return statusMessage_; }
        bool IsAIConfigured() const;
		const std::vector<FGnbProviderOption>& GetProviderCatalog() const { return providerCatalog_; }
		const std::string& GetCurrentProviderId() const { return currentProviderId_; }
		bool SelectProvider(const std::string& providerId);

        const std::vector<FPendingEditorAction>& GetPendingActions() const { return pendingActions_; }
        bool ConfirmPendingAction(uint64_t actionId, EditorContext& ctx);
        bool CancelPendingAction(uint64_t actionId);

        // Get logs from the script executor
        std::vector<ScriptLogEntry> TakeLog();

        // Get the raw AI response for display
        const std::string& GetLastResponse() const { return lastResponse_; }

        // Unified gnb Agent event sink consumed by the panel.
        NextAI::FInMemoryAgentEventSink& GetAgentEventSink() { return agentEventSink_; }

        // Request cancellation of an in-flight Generate. The flag is consumed at the
        // next agent-loop step boundary or async checkpoint.
        void RequestCancel();
        bool IsCancelRequested() const { return cancelRequested_.load(); }

    private:
        void EnsureToolRegistry();
		void RunGnbAgentAsync(const std::string& userPrompt, const EditorContext& ctx);
        void HarvestDeferredActions();
        void TrimConversation();

		std::string BuildSystemPrompt(const EditorContext& ctx);
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

        NextAI::FToolRegistry toolRegistry_;
        bool toolRegistryInitialized_ = false;
        EditorContext* currentContext_ = nullptr;
		NextAI::FGnbAgentClient* gnbClient_ = nullptr;
		std::string gnbSessionId_;
		std::string activeRunId_;
		std::jthread runThread_;
		std::vector<FGnbProviderOption> providerCatalog_;
		std::string currentProviderId_ = "localllm";
		std::string currentModelId_;

        // Persistent multi-turn chat history (user/assistant text turns only). The
        // dynamic system prompt is rebuilt and prepended per request, and agent-loop
        // tool steps are not carried across turns. Accessed on the main thread only.
        std::vector<NextAI::FChatMessage> conversation_;
        static constexpr size_t kMaxConversationMessages = 40;
    };
} // namespace Editor
