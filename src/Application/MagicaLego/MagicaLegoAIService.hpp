#pragma once
#include "Common/CoreMinimal.hpp"
#include <atomic>
#include <mutex>
#include <memory>
#include <glm/glm.hpp>
#include <nlohmann/json_fwd.hpp>

class MagicaLegoGameInstance;

namespace MagicaLego
{
    // Forward declaration for FAIResponse
    struct FAIResponse;

    // AI Provider type enumeration
    enum class EAIProviderType
    {
        Gemini,
        Ollama,
        Zhipu
    };

    // AI Provider abstract interface
    class IAIProvider
    {
    public:
        virtual ~IAIProvider() = default;
        virtual bool Initialize(const nlohmann::json& config) = 0;
        virtual bool IsConfigured() const = 0;
        virtual FAIResponse Generate(const std::string& prompt) = 0;
        virtual std::string GetName() const = 0;
    };

    // Legacy config struct (for backward compatibility)
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

    // Gemini Provider implementation
    class FGeminiProvider : public IAIProvider
    {
    public:
        bool Initialize(const nlohmann::json& config) override;
        bool IsConfigured() const override;
        FAIResponse Generate(const std::string& prompt) override;
        std::string GetName() const override { return "Gemini"; }

    private:
        std::string apiKey_;
        std::string model_ = "gemini-1.5-flash";
        std::string endpoint_ = "https://generativelanguage.googleapis.com/v1beta";
        bool configured_ = false;
    };

    // Ollama Provider implementation
    class FOllamaProvider : public IAIProvider
    {
    public:
        bool Initialize(const nlohmann::json& config) override;
        bool IsConfigured() const override;
        FAIResponse Generate(const std::string& prompt) override;
        std::string GetName() const override { return "Ollama"; }

    private:
        std::string endpoint_ = "http://localhost:11434";
        std::string model_ = "llama3.2";
        bool configured_ = false;
    };

    // Zhipu (智谱AI) Provider implementation
    class FZhipuProvider : public IAIProvider
    {
    public:
        bool Initialize(const nlohmann::json& config) override;
        bool IsConfigured() const override;
        FAIResponse Generate(const std::string& prompt) override;
        std::string GetName() const override { return "Zhipu"; }

    private:
        std::string apiKey_;
        std::string model_ = "glm-4-flash";
        std::string endpoint_ = "https://open.bigmodel.cn/api/paas/v4";
        bool configured_ = false;
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

        // Provider management
        std::string GetProviderName() const;
        EAIProviderType GetProviderType() const { return providerType_; }
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

        // Factory method to create provider
        std::unique_ptr<IAIProvider> CreateProvider(EAIProviderType type);
        nlohmann::json GetProviderConfig(EAIProviderType type) const;

        void UpdateProviderConfigCache();  // Update cached config status for all providers

        MagicaLegoGameInstance* gameInstance_;
        std::unique_ptr<IAIProvider> provider_;
        EAIProviderType providerType_ = EAIProviderType::Gemini;
        std::unique_ptr<nlohmann::json> fullConfig_;  // Store full config for provider switching
        std::map<EAIProviderType, bool> providerConfigCache_;  // Cached config status
        bool configured_ = false;
        EAIStatus status_ = EAIStatus::NotConfigured;
        std::string statusMessage_;

        // Async state
        std::atomic<bool> hasPendingResult_{false};
        FAIResponse pendingResult_;
        std::mutex resultMutex_;
    };
}
