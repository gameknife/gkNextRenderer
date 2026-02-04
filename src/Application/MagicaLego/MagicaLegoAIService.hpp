#pragma once
#include "Common/CoreMinimal.hpp"
#include <atomic>
#include <mutex>

class MagicaLegoGameInstance;

namespace MagicaLego
{
    struct FAIConfig
    {
        std::string apiKey;
        std::string model = "gemini-1.5-flash";
        std::string endpoint = "https://generativelanguage.googleapis.com/v1beta";
    };

    struct FAIResponse
    {
        bool success;
        std::string script;
        std::string message;

        static FAIResponse Success(const std::string& generatedScript)
        {
            return {true, generatedScript, ""};
        }

        static FAIResponse Failure(const std::string& errorMessage)
        {
            return {false, "", errorMessage};
        }
    };

    enum class EAIStatus
    {
        NotConfigured,
        Ready,
        Generating,
        Error
    };

    class FAIService
    {
    public:
        explicit FAIService(MagicaLegoGameInstance* gi);
        ~FAIService() = default;

        bool LoadConfig();
        bool IsConfigured() const { return configured_; }
        EAIStatus GetStatus() const { return status_; }
        const std::string& GetStatusMessage() const { return statusMessage_; }

        // Synchronous API call (blocks until response)
        FAIResponse GenerateScript(const std::string& prompt);

        // Async wrapper using callback
        void GenerateScriptAsync(const std::string& prompt,
                                 std::function<void(FAIResponse)> callback);

        // Check and get async result
        bool HasPendingResult() const { return hasPendingResult_; }
        FAIResponse GetPendingResult();

    private:
        std::string BuildSystemPrompt();
        FAIResponse CallGeminiAPI(const std::string& userPrompt);
        std::string ExtractScriptFromResponse(const std::string& responseText);

        MagicaLegoGameInstance* gameInstance_;
        FAIConfig config_;
        bool configured_ = false;
        EAIStatus status_ = EAIStatus::NotConfigured;
        std::string statusMessage_;

        // Async state
        std::atomic<bool> hasPendingResult_{false};
        FAIResponse pendingResult_;
        std::mutex resultMutex_;
    };
}
