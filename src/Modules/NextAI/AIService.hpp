#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextAI/AI/AIChat.hpp"

#include <functional>
#include <mutex>
#include <thread>

namespace NextAI
{
    struct FAIResponse
    {
        bool success = false;
        std::string text;
        std::string message;
        double elapsedMs = 0.0;
        static FAIResponse Success(const std::string& text, double elapsedMs = 0.0) { return {true, text, "", elapsedMs}; }
        static FAIResponse Failure(const std::string& message, double elapsedMs = 0.0) { return {false, "", message, elapsedMs}; }
    };

    enum class EAIStatus { NotConfigured, Ready, Generating, Error };

    struct FAIProviderDescriptor
    {
        std::string id;
        std::string displayName;
        std::string kind;
        std::string defaultModel;
        std::vector<std::string> models;
        bool configured = false;
        bool available = false;
    };

    class FGnbAIClient;

    // Client-only compatibility facade. Provider routing, credentials, sessions and
    // inference all live in the gnb sidecar; this class contains no provider protocol.
    class FAIService
    {
    public:
        FAIService();
        explicit FAIService(std::string ignoredLegacyConfigPath);
        ~FAIService();

        bool LoadConfig();
        bool IsConfigured() const { return configured_; }
        EAIStatus GetStatus() const { return status_; }
        const std::string& GetStatusMessage() const { return statusMessage_; }

        FAIResponse GenerateText(const std::string& prompt);
        void GenerateTextAsync(const std::string& prompt, std::function<void(FAIResponse)> callback);
        FAIResponse GenerateStructuredText(const std::string& prompt, std::string_view schemaName,
                                           std::string_view jsonSchema);
        void GenerateStructuredTextAsync(const std::string& prompt, std::string schemaName,
                                         std::string jsonSchema, std::function<void(FAIResponse)> callback);
        FChatResponse Chat(const FChatRequest& request);
        FChatResponse ChatStream(const FChatRequest& request, FChatStreamCallback onDelta);
        bool Cancel(const std::string& runId);

        std::string GetProviderName() const { return currentProviderId_; }
        const std::string& GetProviderId() const { return currentProviderId_; }
        bool SwitchProvider(const std::string& providerId);
        bool IsProviderConfigured(const std::string& providerId) const;
        std::vector<std::string> GetProviderModels(const std::string& providerId) const;
        std::string GetCurrentModel() const { return currentModelId_; }
        bool SetCurrentModel(std::string model);
        bool SetProfile(std::string profileId);
        const std::vector<FAIProviderDescriptor>& GetAvailableProviders() const { return providers_; }

    private:
        bool RefreshCatalog();
        bool RecreateSession();
        FChatResponse ChatViaGnb(const FChatRequest& request, FChatStreamCallback onDelta = {});

        std::unique_ptr<FGnbAIClient> client_;
        std::vector<FAIProviderDescriptor> providers_;
        std::string sessionId_;
        std::string currentProfileId_ = "general";
        std::string currentProviderId_;
        std::string currentModelId_;
        bool configured_ = false;
        EAIStatus status_ = EAIStatus::NotConfigured;
        std::string statusMessage_;
        std::mutex asyncThreadsMutex_;
        std::vector<std::jthread> asyncThreads_;
    };
}
