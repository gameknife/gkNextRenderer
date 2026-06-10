#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Subsystems/AI/AIChat.hpp"
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace NextAI
{
    enum class EAIProviderType
    {
        Gemini,
        Ollama,
        Zhipu,
        DeepSeek,
        OpenAI,
        LocalLlama
    };

    struct FAIResponse
    {
        bool success;
        std::string text;
        std::string message;

        static FAIResponse Success(const std::string& generatedText)
        {
            return {true, generatedText, ""};
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

    class IAIProvider;

    class FAIService
    {
    public:
        FAIService();
        explicit FAIService(std::string configPath);
        ~FAIService();

        bool LoadConfig();
        bool IsConfigured() const { return configured_; }
        EAIStatus GetStatus() const { return status_; }
        const std::string& GetStatusMessage() const { return statusMessage_; }

        FAIResponse GenerateText(const std::string& prompt);
        void GenerateTextAsync(const std::string& prompt, std::function<void(FAIResponse)> callback);

        // Multi-message chat with optional tool calling (used by FAgentLoop).
        FChatResponse Chat(const FChatRequest& request);
        FChatResponse ChatStream(const FChatRequest& request, FChatStreamCallback onDelta);
        bool SupportsTools() const;

        std::string GetProviderName() const;
        EAIProviderType GetProviderType() const { return providerType_; }
        bool SwitchProvider(EAIProviderType type);
        bool IsProviderConfigured(EAIProviderType type) const;
        std::vector<std::string> GetProviderModels(EAIProviderType type) const;
        std::string GetCurrentModel() const;
        bool SetCurrentModel(std::string model);
        static std::vector<std::pair<EAIProviderType, std::string>> GetAvailableProviders();
        static std::string ProviderTypeToString(EAIProviderType type);
        static EAIProviderType StringToProviderType(const std::string& name);

    private:
        FAIResponse CallProvider(const std::string& prompt);
        std::unique_ptr<IAIProvider> CreateProvider(EAIProviderType type);
        nlohmann::json GetProviderConfig(EAIProviderType type) const;
        std::string GetProviderDefaultModel(EAIProviderType type) const;
        void UpdateProviderConfigCache();

        std::string configPath_ = "assets/configs/ai_config.json";
        std::unique_ptr<IAIProvider> provider_;
        EAIProviderType providerType_ = EAIProviderType::Gemini;
        std::unique_ptr<nlohmann::json> fullConfig_;
        std::map<EAIProviderType, bool> providerConfigCache_;
        std::map<EAIProviderType, std::vector<std::string>> providerModels_;
        std::map<EAIProviderType, std::string> providerModelSelection_;
        bool configured_ = false;
        EAIStatus status_ = EAIStatus::NotConfigured;
        std::string statusMessage_;
    };
}
