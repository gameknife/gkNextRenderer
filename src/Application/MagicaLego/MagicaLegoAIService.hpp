#pragma once
#include "Common/CoreMinimal.hpp"
#include <atomic>
#include <mutex>
#include <glm/glm.hpp>

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

    // Color semantic description for vocabulary
    struct FColorSemantic
    {
        std::string colorCode;      // e.g., "#119"
        std::string colorName;      // e.g., "grass green"
        std::string category;       // e.g., "nature", "building", "accent"
        std::string suggestedUse;   // e.g., "grass, foliage, leaves"
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

        // Generate with existing scene context
        FAIResponse GenerateScriptWithContext(const std::string& prompt);

        // Async wrapper using callback
        void GenerateScriptAsync(const std::string& prompt,
                                 std::function<void(FAIResponse)> callback);

        // Async with context
        void GenerateScriptWithContextAsync(const std::string& prompt,
                                            std::function<void(FAIResponse)> callback);

        // Check and get async result
        bool HasPendingResult() const { return hasPendingResult_; }
        FAIResponse GetPendingResult();

        // Get color vocabulary for display
        std::string GetColorVocabulary() { return BuildColorVocabulary(); }
        std::vector<FColorSemantic> GetColorSemantics();

    private:
        std::string BuildSystemPrompt();
        std::string BuildContextPrompt(const std::string& userPrompt);
        std::string BuildColorVocabulary();
        FColorSemantic AnalyzeColor(const std::string& colorCode, glm::vec4 rgba);
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
