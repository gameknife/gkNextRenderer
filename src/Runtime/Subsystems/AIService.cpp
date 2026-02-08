#include "Runtime/Subsystems/AIService.hpp"
#include "Runtime/Config/AISettings.hpp"
#include "Utilities/FileHelper.hpp"
#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <utility>

using json = nlohmann::json;

namespace
{
    size_t AIServiceWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        static_cast<std::string*>(userp)->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }
}

namespace NextAI
{
    class IAIProvider
    {
    public:
        virtual ~IAIProvider() = default;
        virtual bool Initialize(const nlohmann::json& config) = 0;
        virtual bool IsConfigured() const = 0;
        virtual FAIResponse Generate(const std::string& prompt) = 0;
        virtual std::string GetName() const = 0;
    };

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

    class FDeepSeekProvider : public IAIProvider
    {
    public:
        bool Initialize(const nlohmann::json& config) override;
        bool IsConfigured() const override;
        FAIResponse Generate(const std::string& prompt) override;
        std::string GetName() const override { return "DeepSeek"; }

    private:
        std::string apiKey_;
        std::string model_ = "deepseek-chat";
        std::string endpoint_ = "https://api.deepseek.com/v1";
        bool configured_ = false;
    };

    bool FGeminiProvider::Initialize(const nlohmann::json& config)
    {
        try
        {
            if (config.contains("apiKey"))
            {
                apiKey_ = config["apiKey"].get<std::string>();
            }
            if (config.contains("model"))
            {
                model_ = config["model"].get<std::string>();
            }
            if (config.contains("endpoint"))
            {
                endpoint_ = config["endpoint"].get<std::string>();
            }

            if (apiKey_.empty() || apiKey_ == "YOUR_GOOGLE_API_KEY")
            {
                configured_ = false;
                return false;
            }

            configured_ = true;
            SPDLOG_INFO("Gemini Provider initialized with model: {}", model_);
            return true;
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to initialize Gemini Provider: {}", e.what());
            configured_ = false;
            return false;
        }
    }

    bool FGeminiProvider::IsConfigured() const
    {
        return configured_;
    }

    FAIResponse FGeminiProvider::Generate(const std::string& prompt)
    {
        if (!configured_)
        {
            return FAIResponse::Failure("Gemini provider not configured");
        }

        std::string url = fmt::format("{}/models/{}:generateContent?key={}",
            endpoint_, model_, apiKey_);

        json requestBody = {
            {"contents", json::array({
                {{"role", "user"}, {"parts", json::array({{{"text", prompt}}})}}
            })},
            {"generationConfig", {
                {"temperature", 0.7},
                {"maxOutputTokens", AIConfig::MaxOutputTokens}
            }}
        };

        std::string requestStr = requestBody.dump();
        std::string responseBuffer;

        CURL* curl = curl_easy_init();
        if (!curl)
        {
            return FAIResponse::Failure("Failed to initialize CURL");
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestStr.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AIServiceWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, AIConfig::RequestTimeoutSeconds);

        CURLcode res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            return FAIResponse::Failure(fmt::format("Network error: {}", curl_easy_strerror(res)));
        }

        try
        {
            json response = json::parse(responseBuffer);

            if (response.contains("error"))
            {
                std::string errorMsg = response["error"]["message"].get<std::string>();
                return FAIResponse::Failure(fmt::format("API error: {}", errorMsg));
            }

            if (response.contains("candidates") && !response["candidates"].empty())
            {
                auto& candidate = response["candidates"][0];
                if (candidate.contains("content") && candidate["content"].contains("parts"))
                {
                    auto& parts = candidate["content"]["parts"];
                    if (!parts.empty() && parts[0].contains("text"))
                    {
                        std::string generatedText = parts[0]["text"].get<std::string>();
                        return FAIResponse::Success(generatedText);
                    }
                }
            }

            return FAIResponse::Failure("Unexpected API response format");
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse Gemini response: {}", e.what());
            return FAIResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    bool FOllamaProvider::Initialize(const nlohmann::json& config)
    {
        try
        {
            if (config.contains("endpoint"))
            {
                endpoint_ = config["endpoint"].get<std::string>();
            }
            if (config.contains("model"))
            {
                model_ = config["model"].get<std::string>();
            }

            if (endpoint_.empty())
            {
                configured_ = false;
                return false;
            }

            configured_ = true;
            SPDLOG_INFO("Ollama Provider initialized with model: {} at {}", model_, endpoint_);
            return true;
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to initialize Ollama Provider: {}", e.what());
            configured_ = false;
            return false;
        }
    }

    bool FOllamaProvider::IsConfigured() const
    {
        return configured_;
    }

    FAIResponse FOllamaProvider::Generate(const std::string& prompt)
    {
        if (!configured_)
        {
            return FAIResponse::Failure("Ollama provider not configured");
        }

        std::string url = endpoint_ + "/api/generate";

        json requestBody = {
            {"model", model_},
            {"prompt", prompt},
            {"stream", false}
        };

        std::string requestStr = requestBody.dump();
        std::string responseBuffer;

        CURL* curl = curl_easy_init();
        if (!curl)
        {
            return FAIResponse::Failure("Failed to initialize CURL");
        }

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestStr.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AIServiceWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, AIConfig::RequestTimeoutSeconds);

        CURLcode res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            return FAIResponse::Failure(fmt::format("Network error: {}", curl_easy_strerror(res)));
        }

        try
        {
            json response = json::parse(responseBuffer);

            if (response.contains("error"))
            {
                std::string errorMsg = response["error"].get<std::string>();
                return FAIResponse::Failure(fmt::format("Ollama error: {}", errorMsg));
            }

            if (response.contains("response"))
            {
                std::string generatedText = response["response"].get<std::string>();
                return FAIResponse::Success(generatedText);
            }

            return FAIResponse::Failure("Unexpected Ollama response format");
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse Ollama response: {}", e.what());
            return FAIResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    bool FZhipuProvider::Initialize(const nlohmann::json& config)
    {
        try
        {
            if (config.contains("apiKey"))
            {
                apiKey_ = config["apiKey"].get<std::string>();
            }
            if (config.contains("model"))
            {
                model_ = config["model"].get<std::string>();
            }
            if (config.contains("endpoint"))
            {
                endpoint_ = config["endpoint"].get<std::string>();
            }

            if (apiKey_.empty() || apiKey_ == "YOUR_ZHIPU_API_KEY")
            {
                configured_ = false;
                return false;
            }

            configured_ = true;
            SPDLOG_INFO("Zhipu Provider initialized with model: {}", model_);
            return true;
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to initialize Zhipu Provider: {}", e.what());
            configured_ = false;
            return false;
        }
    }

    bool FZhipuProvider::IsConfigured() const
    {
        return configured_;
    }

    FAIResponse FZhipuProvider::Generate(const std::string& prompt)
    {
        if (!configured_)
        {
            return FAIResponse::Failure("Zhipu provider not configured");
        }

        std::string url = endpoint_ + "/chat/completions";

        json requestBody = {
            {"model", model_},
            {"messages", json::array({
                {{"role", "user"}, {"content", prompt}}
            })}
        };

        std::string requestStr = requestBody.dump();
        std::string responseBuffer;

        CURL* curl = curl_easy_init();
        if (!curl)
        {
            return FAIResponse::Failure("Failed to initialize CURL");
        }

        std::string authHeader = fmt::format("Authorization: Bearer {}", apiKey_);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, authHeader.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestStr.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AIServiceWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, AIConfig::RequestTimeoutSeconds);

        CURLcode res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            return FAIResponse::Failure(fmt::format("Network error: {}", curl_easy_strerror(res)));
        }

        try
        {
            json response = json::parse(responseBuffer);

            if (response.contains("error"))
            {
                std::string errorMsg;
                if (response["error"].is_object() && response["error"].contains("message"))
                {
                    errorMsg = response["error"]["message"].get<std::string>();
                }
                else
                {
                    errorMsg = response["error"].dump();
                }
                return FAIResponse::Failure(fmt::format("Zhipu error: {}", errorMsg));
            }

            if (response.contains("choices") && !response["choices"].empty())
            {
                auto& choice = response["choices"][0];
                if (choice.contains("message") && choice["message"].contains("content"))
                {
                    std::string generatedText = choice["message"]["content"].get<std::string>();
                    return FAIResponse::Success(generatedText);
                }
            }

            return FAIResponse::Failure("Unexpected Zhipu response format");
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse Zhipu response: {}", e.what());
            return FAIResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    bool FDeepSeekProvider::Initialize(const nlohmann::json& config)
    {
        try
        {
            if (config.contains("apiKey"))
            {
                apiKey_ = config["apiKey"].get<std::string>();
            }
            if (config.contains("model"))
            {
                model_ = config["model"].get<std::string>();
            }
            if (config.contains("endpoint"))
            {
                endpoint_ = config["endpoint"].get<std::string>();
            }

            if (apiKey_.empty() || apiKey_ == "YOUR_DEEPSEEK_API_KEY")
            {
                configured_ = false;
                return false;
            }

            configured_ = true;
            SPDLOG_INFO("DeepSeek Provider initialized with model: {}", model_);
            return true;
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to initialize DeepSeek Provider: {}", e.what());
            configured_ = false;
            return false;
        }
    }

    bool FDeepSeekProvider::IsConfigured() const
    {
        return configured_;
    }

    FAIResponse FDeepSeekProvider::Generate(const std::string& prompt)
    {
        if (!configured_)
        {
            return FAIResponse::Failure("DeepSeek provider not configured");
        }

        std::string url = endpoint_ + "/chat/completions";

        json requestBody = {
            {"model", model_},
            {"messages", json::array({
                {{"role", "user"}, {"content", prompt}}
            })}
        };

        std::string requestStr = requestBody.dump();
        std::string responseBuffer;

        CURL* curl = curl_easy_init();
        if (!curl)
        {
            return FAIResponse::Failure("Failed to initialize CURL");
        }

        std::string authHeader = fmt::format("Authorization: Bearer {}", apiKey_);

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, authHeader.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, requestStr.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AIServiceWriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, AIConfig::RequestTimeoutSeconds);

        CURLcode res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            return FAIResponse::Failure(fmt::format("Network error: {}", curl_easy_strerror(res)));
        }

        try
        {
            json response = json::parse(responseBuffer);

            if (response.contains("error"))
            {
                std::string errorMsg;
                if (response["error"].is_object() && response["error"].contains("message"))
                {
                    errorMsg = response["error"]["message"].get<std::string>();
                }
                else
                {
                    errorMsg = response["error"].dump();
                }
                return FAIResponse::Failure(fmt::format("DeepSeek error: {}", errorMsg));
            }

            if (response.contains("choices") && !response["choices"].empty())
            {
                auto& choice = response["choices"][0];
                if (choice.contains("message") && choice["message"].contains("content"))
                {
                    std::string generatedText = choice["message"]["content"].get<std::string>();
                    return FAIResponse::Success(generatedText);
                }
            }

            return FAIResponse::Failure("Unexpected DeepSeek response format");
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse DeepSeek response: {}", e.what());
            return FAIResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    FAIService::FAIService()
    {
        LoadConfig();
    }

    FAIService::FAIService(std::string configPath)
        : configPath_(std::move(configPath))
    {
        LoadConfig();
    }

    FAIService::~FAIService() = default;

    std::unique_ptr<IAIProvider> FAIService::CreateProvider(EAIProviderType type)
    {
        switch (type)
        {
        case EAIProviderType::Gemini:
            return std::make_unique<FGeminiProvider>();
        case EAIProviderType::Ollama:
            return std::make_unique<FOllamaProvider>();
        case EAIProviderType::Zhipu:
            return std::make_unique<FZhipuProvider>();
        case EAIProviderType::DeepSeek:
            return std::make_unique<FDeepSeekProvider>();
        default:
            return std::make_unique<FGeminiProvider>();
        }
    }

    std::string FAIService::GetProviderName() const
    {
        if (provider_)
        {
            return provider_->GetName();
        }
        return "None";
    }

    std::string FAIService::ProviderTypeToString(EAIProviderType type)
    {
        switch (type)
        {
        case EAIProviderType::Gemini: return "gemini";
        case EAIProviderType::Ollama: return "ollama";
        case EAIProviderType::Zhipu: return "zhipu";
        case EAIProviderType::DeepSeek: return "deepseek";
        default: return "gemini";
        }
    }

    EAIProviderType FAIService::StringToProviderType(const std::string& name)
    {
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower == "ollama") return EAIProviderType::Ollama;
        if (lower == "zhipu") return EAIProviderType::Zhipu;
        if (lower == "deepseek") return EAIProviderType::DeepSeek;
        return EAIProviderType::Gemini;
    }

    std::vector<std::pair<EAIProviderType, std::string>> FAIService::GetAvailableProviders()
    {
        return {
            {EAIProviderType::Gemini, "Gemini"},
            {EAIProviderType::Ollama, "Ollama"},
            {EAIProviderType::Zhipu, "Zhipu"},
            {EAIProviderType::DeepSeek, "DeepSeek"}
        };
    }

    nlohmann::json FAIService::GetProviderConfig(EAIProviderType type) const
    {
        if (!fullConfig_)
        {
            return nlohmann::json::object();
        }

        std::string configKey = ProviderTypeToString(type);

        if (fullConfig_->contains(configKey))
        {
            return (*fullConfig_)[configKey];
        }

        if (type == EAIProviderType::Gemini)
        {
            return *fullConfig_;
        }

        return nlohmann::json::object();
    }

    bool FAIService::IsProviderConfigured(EAIProviderType type) const
    {
        auto it = providerConfigCache_.find(type);
        if (it != providerConfigCache_.end())
        {
            return it->second;
        }
        return false;
    }

    bool FAIService::TryGetVoiceInputConfig(FVoiceInputConfig& outConfig) const
    {
        if (!hasVoiceInputConfig_)
        {
            return false;
        }

        outConfig = voiceInputConfig_;
        return true;
    }

    void FAIService::UpdateProviderConfigCache()
    {
        providerConfigCache_.clear();

        for (const auto& [type, name] : GetAvailableProviders())
        {
            auto tempProvider = CreateProvider(type);
            if (tempProvider)
            {
                nlohmann::json config = GetProviderConfig(type);
                bool configured = tempProvider->Initialize(config);
                providerConfigCache_[type] = configured;
            }
            else
            {
                providerConfigCache_[type] = false;
            }
        }
    }

    bool FAIService::SwitchProvider(EAIProviderType type)
    {
        if (status_ == EAIStatus::Generating)
        {
            SPDLOG_WARN("Cannot switch provider while generating");
            return false;
        }

        auto newProvider = CreateProvider(type);
        if (!newProvider)
        {
            SPDLOG_ERROR("Failed to create provider: {}", ProviderTypeToString(type));
            return false;
        }

        nlohmann::json config = GetProviderConfig(type);

        if (!newProvider->Initialize(config))
        {
            status_ = EAIStatus::NotConfigured;
            statusMessage_ = fmt::format("{} provider not configured", newProvider->GetName());
            configured_ = false;
            SPDLOG_WARN("Failed to initialize provider: {}", newProvider->GetName());
            return false;
        }

        provider_ = std::move(newProvider);
        providerType_ = type;
        configured_ = true;
        status_ = EAIStatus::Ready;
        statusMessage_ = fmt::format("{} ready", provider_->GetName());

        SPDLOG_INFO("Switched to AI provider: {}", provider_->GetName());
        return true;
    }

    bool FAIService::LoadConfig()
    {
        std::string configPath = Utilities::FileHelper::GetPlatformFilePath(configPath_.c_str());
        std::ifstream file(configPath);

        if (!file.is_open())
        {
            status_ = EAIStatus::NotConfigured;
            statusMessage_ = "Config file not found";
            configured_ = false;
            return false;
        }

        try
        {
            json j;
            file >> j;

            hasVoiceInputConfig_ = false;
            voiceInputConfig_ = FVoiceInputConfig{};

            if (j.contains("voiceInput") && j["voiceInput"].is_object())
            {
                const json& voiceConfig = j["voiceInput"];

                auto readBool = [&voiceConfig](const char* key, bool& value) {
                    if (!voiceConfig.contains(key))
                    {
                        return;
                    }
                    if (voiceConfig[key].is_boolean())
                    {
                        value = voiceConfig[key].get<bool>();
                    }
                    else
                    {
                        SPDLOG_WARN("voiceInput.{} expects boolean", key);
                    }
                };

                auto readInt = [&voiceConfig](const char* key, int& value) {
                    if (!voiceConfig.contains(key))
                    {
                        return;
                    }
                    if (voiceConfig[key].is_number_integer())
                    {
                        value = voiceConfig[key].get<int>();
                    }
                    else
                    {
                        SPDLOG_WARN("voiceInput.{} expects integer", key);
                    }
                };

                auto readString = [&voiceConfig](const char* key, std::string& value) {
                    if (!voiceConfig.contains(key))
                    {
                        return;
                    }
                    if (voiceConfig[key].is_string())
                    {
                        value = voiceConfig[key].get<std::string>();
                    }
                    else
                    {
                        SPDLOG_WARN("voiceInput.{} expects string", key);
                    }
                };

                readBool("enabled", voiceInputConfig_.enabled);
                readString("model", voiceInputConfig_.model);
                readString("language", voiceInputConfig_.language);
                readInt("threads", voiceInputConfig_.threads);
                readInt("maxRecordSeconds", voiceInputConfig_.maxRecordSeconds);
                readInt("sampleRate", voiceInputConfig_.sampleRate);
                readBool("autoSend", voiceInputConfig_.autoSend);
                readBool("keepTempFiles", voiceInputConfig_.keepTempFiles);

                // Backward compatibility: old config used modelPath.
                if (voiceConfig.contains("modelPath") && voiceConfig["modelPath"].is_string())
                {
                    std::string legacyPath = voiceConfig["modelPath"].get<std::string>();
                    std::string lowerPath = legacyPath;
                    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
                    if (lowerPath.find("tiny") != std::string::npos)
                    {
                        voiceInputConfig_.model = "tiny";
                    }
                    else if (lowerPath.find("small") != std::string::npos)
                    {
                        voiceInputConfig_.model = "small";
                    }
                    else
                    {
                        voiceInputConfig_.model = "base";
                    }
                }

                if (voiceInputConfig_.language.empty())
                {
                    voiceInputConfig_.language = "zh";
                }
                if (voiceInputConfig_.model.empty())
                {
                    voiceInputConfig_.model = "base";
                }
                if (voiceInputConfig_.threads <= 0)
                {
                    voiceInputConfig_.threads = 4;
                }
                if (voiceInputConfig_.maxRecordSeconds <= 0)
                {
                    voiceInputConfig_.maxRecordSeconds = 20;
                }
                if (voiceInputConfig_.sampleRate <= 0)
                {
                    voiceInputConfig_.sampleRate = 16000;
                }

                hasVoiceInputConfig_ = true;
            }

            fullConfig_ = std::make_unique<nlohmann::json>(j);
            UpdateProviderConfigCache();

            auto activateProvider = [this](EAIProviderType type) -> bool
            {
                auto newProvider = CreateProvider(type);
                if (!newProvider)
                {
                    SPDLOG_ERROR("Failed to create provider: {}", ProviderTypeToString(type));
                    return false;
                }

                nlohmann::json providerConfig = GetProviderConfig(type);
                if (!newProvider->Initialize(providerConfig))
                {
                    SPDLOG_WARN("Failed to initialize provider: {}", newProvider->GetName());
                    return false;
                }

                provider_ = std::move(newProvider);
                providerType_ = type;
                configured_ = true;
                status_ = EAIStatus::Ready;
                statusMessage_ = fmt::format("{} ready", provider_->GetName());
                return true;
            };

            std::string providerName = "gemini";
            if (j.contains("provider"))
            {
                providerName = j["provider"].get<std::string>();
            }

            const EAIProviderType preferredType = StringToProviderType(providerName);
            if (activateProvider(preferredType))
            {
                SPDLOG_INFO("AI Service configured with provider: {}", provider_->GetName());
                return true;
            }

            for (const auto& [type, name] : GetAvailableProviders())
            {
                if (type == preferredType || !IsProviderConfigured(type))
                {
                    continue;
                }

                SPDLOG_WARN("Preferred AI provider '{}' is unavailable, fallback to '{}'",
                    ProviderTypeToString(preferredType), name);

                if (activateProvider(type))
                {
                    SPDLOG_INFO("AI Service configured with fallback provider: {}", provider_->GetName());
                    return true;
                }
            }

            provider_.reset();
            providerType_ = preferredType;
            status_ = EAIStatus::NotConfigured;
            statusMessage_ = fmt::format("No configured AI provider available (preferred: {})",
                ProviderTypeToString(preferredType));
            configured_ = false;
            SPDLOG_WARN("AI Service has no configured provider. Preferred: {}", ProviderTypeToString(preferredType));
            return false;
        }
        catch (const std::exception& e)
        {
            status_ = EAIStatus::Error;
            statusMessage_ = fmt::format("Config parse error: {}", e.what());
            configured_ = false;
            SPDLOG_ERROR("Failed to parse AI config: {}", e.what());
            return false;
        }
    }

    FAIResponse FAIService::CallProvider(const std::string& prompt)
    {
        if (!configured_ || !provider_)
        {
            return FAIResponse::Failure("AI service not configured");
        }

        return provider_->Generate(prompt);
    }

    FAIResponse FAIService::GenerateText(const std::string& prompt)
    {
        status_ = EAIStatus::Generating;
        statusMessage_ = "Generating...";

        auto response = CallProvider(prompt);

        if (response.success)
        {
            status_ = EAIStatus::Ready;
            statusMessage_ = "Ready";
        }
        else
        {
            status_ = EAIStatus::Error;
            statusMessage_ = response.message;
        }

        return response;
    }

    void FAIService::GenerateTextAsync(const std::string& prompt, std::function<void(FAIResponse)> callback)
    {
        status_ = EAIStatus::Generating;
        statusMessage_ = "Generating...";

        std::thread([this, prompt, callback]()
        {
            auto response = CallProvider(prompt);

            if (response.success)
            {
                status_ = EAIStatus::Ready;
                statusMessage_ = "Ready";
            }
            else
            {
                status_ = EAIStatus::Error;
                statusMessage_ = response.message;
            }

            if (callback)
            {
                callback(response);
            }
        }).detach();
    }
}
