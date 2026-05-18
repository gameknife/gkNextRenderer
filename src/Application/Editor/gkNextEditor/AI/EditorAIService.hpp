#pragma once

#include "Common/CoreMinimal.hpp"
#include "Application/Editor/gkNextEditor/AI/EditorScriptExecutor.hpp"
#include "Runtime/Subsystems/AIService.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
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

        // Build the system prompt with scene context
        std::string BuildSystemPrompt(const EditorContext& ctx);

        // Send user prompt to AI asynchronously
        void GenerateAsync(const std::string& userPrompt, const EditorContext& ctx);

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

    private:
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
    };
} // namespace Editor
