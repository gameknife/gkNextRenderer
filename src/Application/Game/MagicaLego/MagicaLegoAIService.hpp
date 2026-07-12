#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/AIService.hpp"
#include <atomic>
#include <functional>
#include <glm/glm.hpp>
#include <mutex>
#include <string>
#include <vector>
#include <thread>

class MagicaLegoGameInstance;

namespace MagicaLego
{
    using EAIStatus = NextAI::EAIStatus;

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

    struct FColorSemantic
    {
        std::string colorCode;
        std::string colorName;
        std::string category;
        std::string suggestedUse;
    };

    class FAIService
    {
    public:
        explicit FAIService(MagicaLegoGameInstance* gi);
		~FAIService();

        bool LoadConfig();
        bool IsConfigured() const;
        EAIStatus GetStatus() const;
        const std::string& GetStatusMessage() const;

        FAIResponse GenerateScript(const std::string& prompt);
        FAIResponse GenerateScriptWithContext(const std::string& prompt);

        void GenerateScriptAsync(const std::string& prompt,
                                 std::function<void(FAIResponse)> callback);
        void GenerateScriptWithContextAsync(const std::string& prompt,
                                            std::function<void(FAIResponse)> callback);

        bool HasPendingResult() const { return hasPendingResult_; }
        FAIResponse GetPendingResult();

        std::string GetColorVocabulary() { return BuildColorVocabulary(); }
        std::vector<FColorSemantic> GetColorSemantics();

        std::string GetProviderName() const;
		std::string GetProviderId() const;
		bool SwitchProvider(const std::string& providerId);
		bool IsProviderConfigured(const std::string& providerId) const;
		std::vector<NextAI::FAIProviderDescriptor> GetAvailableProviders() const;

    private:
        std::string BuildSystemPrompt();
        std::string BuildContextPrompt(const std::string& userPrompt);
        std::string BuildColorVocabulary();
        FColorSemantic AnalyzeColor(const std::string& colorCode, glm::vec4 rgba);
        FAIResponse CallProvider(const std::string& userPrompt);
        std::string ExtractScriptFromResponse(const std::string& responseText);

        MagicaLegoGameInstance* gameInstance_;
        NextAI::FAIService* aiService_ = nullptr;

        std::atomic<bool> hasPendingResult_{false};
        FAIResponse pendingResult_;
        std::mutex resultMutex_;
		std::jthread worker_;
    };
}
