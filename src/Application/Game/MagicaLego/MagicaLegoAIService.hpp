#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/AIService.hpp"
#include <atomic>
#include <functional>
#include <glm/glm.hpp>
#include <mutex>
#include <string>
#include <vector>

class MagicaLegoGameInstance;

namespace MagicaLego
{
    using EAIProviderType = NextAI::EAIProviderType;
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
        ~FAIService() = default;

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
        EAIProviderType GetProviderType() const;
        bool SwitchProvider(EAIProviderType type);
        bool IsProviderConfigured(EAIProviderType type) const;
        static std::vector<std::pair<EAIProviderType, std::string>> GetAvailableProviders();
        static std::string ProviderTypeToString(EAIProviderType type);
        static EAIProviderType StringToProviderType(const std::string& name);

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
    };
}
