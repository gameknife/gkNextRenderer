#pragma once

#include "Common/CoreMinimal.hpp"
#include "Editor/AI/EditorScriptExecutor.hpp"
#include "Runtime/Subsystems/AIService.hpp"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class NextEngine;

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

    class FEditorAIService
    {
    public:
        explicit FEditorAIService(NextEngine& engine);

        // Build the system prompt with scene context
        std::string BuildSystemPrompt();

        // Send user prompt to AI asynchronously
        void GenerateAsync(const std::string& userPrompt);

        // Poll for completion (call from main thread tick)
        bool HasPendingResult() const { return hasPendingResult_.load(); }

        // Consume pending result and execute it
        void ConsumePendingResult();

        // Direct execution of script text (for manual input)
        void ExecuteDirect(const std::string& input);

        EEditorAIStatus GetStatus() const { return status_; }
        const std::string& GetStatusMessage() const { return statusMessage_; }
        bool IsAIConfigured() const;

        // Get logs from the script executor
        std::vector<ScriptLogEntry> TakeLog();

        // Get the raw AI response for display
        const std::string& GetLastResponse() const { return lastResponse_; }

    private:
        std::string BuildSceneContext();
        std::string BuildAvailableCommands();
        std::string BuildPropertyTypeInfo();
        std::vector<FCodeBlock> ExtractFromResponse(const std::string& response);

        NextEngine& engine_;
        FEditorScriptExecutor executor_;

        EEditorAIStatus status_ = EEditorAIStatus::Idle;
        std::string statusMessage_;
        std::string lastResponse_;

        std::mutex resultMutex_;
        std::atomic<bool> hasPendingResult_{false};
        NextAI::FAIResponse pendingResponse_;
    };
} // namespace Editor
