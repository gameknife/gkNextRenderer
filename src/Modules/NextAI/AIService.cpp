#include "Modules/NextAI/AIService.hpp"
#include "Modules/NextAI/AISettings.hpp"
#include "Modules/NextAI/AI/LlamaPidFile.hpp"
#include "Engine/Runtime/Platform/UserPaths.h"
#include "Engine/Utilities/FileHelper.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
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
        virtual FChatResponse Chat(const FChatRequest& request) = 0;
        virtual FChatResponse ChatStream(const FChatRequest& request, FChatStreamCallback onDelta)
        {
            FChatResponse response = Chat(request);
            if (response.success && onDelta && !response.content.empty())
            {
                onDelta(response.content);
            }
            return response;
        }
        virtual bool SupportsTools() const = 0;
        virtual std::string GetName() const = 0;
    };

    namespace
    {
        struct FHttpResult
        {
            bool ok = false;
            std::string body;
            std::string error;
            long statusCode = 0;
        };

        struct FStreamState
        {
            std::string lineBuffer;
            std::string rawBody;
            std::string content;
            std::string error;
            std::string finishReason;
            FChatStreamCallback onDelta;
        };

        FHttpResult HttpPostJson(const std::string& url, const std::string& jsonBody,
                                 const std::vector<std::string>& extraHeaders = {})
        {
            FHttpResult result;
            CURL* curl = curl_easy_init();
            if (!curl)
            {
                result.error = "Failed to initialize CURL";
                return result;
            }

            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            for (const auto& h : extraHeaders)
            {
                headers = curl_slist_append(headers, h.c_str());
            }

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AIServiceWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, AIConfig::RequestTimeoutSeconds);

            CURLcode res = curl_easy_perform(curl);
            if (res == CURLE_OK)
            {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.statusCode);
                result.ok = true;
            }
            else
            {
                result.error = fmt::format("Network error: {}", curl_easy_strerror(res));
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return result;
        }

        std::string TrimStreamLine(std::string line)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            {
                line.pop_back();
            }
            const size_t b = line.find_first_not_of(" \t");
            if (b == std::string::npos)
            {
                return "";
            }
            return line.substr(b);
        }

        void ConsumeOpenAIStreamLine(FStreamState& state, std::string line)
        {
            line = TrimStreamLine(std::move(line));
            if (line.empty())
            {
                return;
            }
            if (line.rfind("data:", 0) != 0)
            {
                return;
            }

            std::string payload = TrimStreamLine(line.substr(5));
            if (payload == "[DONE]")
            {
                return;
            }

            try
            {
                const json body = json::parse(payload);
                if (body.contains("error"))
                {
                    const auto& err = body["error"];
                    state.error = err.is_object() && err.contains("message") ? err["message"].get<std::string>()
                                                                              : err.dump();
                    return;
                }
                if (!body.contains("choices") || body["choices"].empty())
                {
                    return;
                }

                const auto& choice = body["choices"][0];
                if (choice.contains("finish_reason") && choice["finish_reason"].is_string())
                {
                    state.finishReason = choice["finish_reason"].get<std::string>();
                }
                if (!choice.contains("delta") || !choice["delta"].is_object())
                {
                    return;
                }

                const auto& delta = choice["delta"];
                if (delta.contains("content") && delta["content"].is_string())
                {
                    std::string text = delta["content"].get<std::string>();
                    if (!text.empty())
                    {
                        state.content += text;
                        if (state.onDelta)
                        {
                            state.onDelta(text);
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                state.error = fmt::format("Stream parse error: {}", e.what());
            }
        }

        size_t AIServiceStreamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
        {
            auto* state = static_cast<FStreamState*>(userp);
            const size_t byteCount = size * nmemb;
            const char* data = static_cast<const char*>(contents);
            state->rawBody.append(data, byteCount);
            state->lineBuffer.append(data, byteCount);

            size_t newline = std::string::npos;
            while ((newline = state->lineBuffer.find('\n')) != std::string::npos)
            {
                std::string line = state->lineBuffer.substr(0, newline + 1);
                state->lineBuffer.erase(0, newline + 1);
                ConsumeOpenAIStreamLine(*state, std::move(line));
            }

            return byteCount;
        }

        FChatResponse HttpPostOpenAIChatStream(
            const std::string& url,
            json body,
            const std::vector<std::string>& extraHeaders,
            FChatStreamCallback onDelta)
        {
            body["stream"] = true;

            CURL* curl = curl_easy_init();
            if (!curl)
            {
                return FChatResponse::Failure("Failed to initialize CURL");
            }

            FStreamState state;
            state.onDelta = std::move(onDelta);
            const std::string jsonBody = body.dump();

            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            headers = curl_slist_append(headers, "Accept: text/event-stream");
            for (const std::string& h : extraHeaders)
            {
                headers = curl_slist_append(headers, h.c_str());
            }

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AIServiceStreamWriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, AIConfig::RequestTimeoutSeconds);

            const CURLcode res = curl_easy_perform(curl);
            long statusCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &statusCode);

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (res != CURLE_OK)
            {
                return FChatResponse::Failure(fmt::format("Network error: {}", curl_easy_strerror(res)));
            }
            if (statusCode >= 400)
            {
                return FChatResponse::Failure(fmt::format("HTTP {}: {}", statusCode, state.rawBody));
            }
            if (!state.lineBuffer.empty())
            {
                ConsumeOpenAIStreamLine(state, std::move(state.lineBuffer));
            }
            if (!state.error.empty())
            {
                return FChatResponse::Failure(state.error);
            }

            FChatResponse response;
            response.success = true;
            response.content = std::move(state.content);
            response.finishReason = std::move(state.finishReason);
            return response;
        }

        FAIResponse ChatToLegacyResponse(const FChatResponse& chatResp)
        {
            if (!chatResp.success)
            {
                return FAIResponse::Failure(chatResp.errorMessage);
            }
            return FAIResponse::Success(chatResp.content);
        }

        void ReadModelSetting(const nlohmann::json& config, std::string& model)
        {
            if (config.contains("defaultModel") && config["defaultModel"].is_string())
            {
                model = config["defaultModel"].get<std::string>();
                return;
            }
            if (config.contains("model") && config["model"].is_string())
            {
                model = config["model"].get<std::string>();
            }
        }

        void MergeSecretObject(nlohmann::json& target, const nlohmann::json& secret)
        {
            if (!target.is_object() || !secret.is_object())
            {
                return;
            }

            for (auto it = secret.begin(); it != secret.end(); ++it)
            {
                if (target.contains(it.key()) && target[it.key()].is_object() && it.value().is_object())
                {
                    for (auto field = it.value().begin(); field != it.value().end(); ++field)
                    {
                        target[it.key()][field.key()] = field.value();
                    }
                }
                else
                {
                    target[it.key()] = it.value();
                }
            }
        }

        std::filesystem::path ResolveConfigRelativePath(const std::string& path)
        {
            std::filesystem::path p(path);
            if (p.is_absolute())
            {
                return p;
            }
            return std::filesystem::path(Utilities::FileHelper::GetPlatformFilePath(path.c_str()));
        }

        bool TryReadJsonFile(const std::filesystem::path& path, nlohmann::json& out)
        {
            std::ifstream file(path);
            if (!file.is_open())
            {
                return false;
            }
            file >> out;
            return true;
        }
    }

    class FGeminiProvider : public IAIProvider
    {
    public:
        bool Initialize(const nlohmann::json& config) override;
        bool IsConfigured() const override;
        FAIResponse Generate(const std::string& prompt) override;
        FChatResponse Chat(const FChatRequest& request) override;
        bool SupportsTools() const override { return true; }
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
        FChatResponse Chat(const FChatRequest& request) override;
        bool SupportsTools() const override { return true; }
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
        FChatResponse Chat(const FChatRequest& request) override;
        FChatResponse ChatStream(const FChatRequest& request, FChatStreamCallback onDelta) override;
        bool SupportsTools() const override { return true; }
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
        FChatResponse Chat(const FChatRequest& request) override;
        FChatResponse ChatStream(const FChatRequest& request, FChatStreamCallback onDelta) override;
        bool SupportsTools() const override { return true; }
        std::string GetName() const override { return "DeepSeek"; }

    private:
        std::string apiKey_;
        std::string model_ = "deepseek-chat";
        std::string endpoint_ = "https://api.deepseek.com/v1";
        bool configured_ = false;
    };

    class FOpenAIProvider : public IAIProvider
    {
    public:
        bool Initialize(const nlohmann::json& config) override;
        bool IsConfigured() const override;
        FAIResponse Generate(const std::string& prompt) override;
        FChatResponse Chat(const FChatRequest& request) override;
        FChatResponse ChatStream(const FChatRequest& request, FChatStreamCallback onDelta) override;
        bool SupportsTools() const override { return true; }
        std::string GetName() const override { return "OpenAI"; }

    private:
        std::string apiKey_;
        std::string model_ = "gpt-4.1-mini";
        std::string endpoint_ = "https://api.openai.com/v1";
        bool configured_ = false;
    };

    class FLocalLlamaProvider : public IAIProvider
    {
    public:
        bool Initialize(const nlohmann::json& config) override;
        bool IsConfigured() const override;
        FAIResponse Generate(const std::string& prompt) override;
        FChatResponse Chat(const FChatRequest& request) override;
        FChatResponse ChatStream(const FChatRequest& request, FChatStreamCallback onDelta) override;
        bool SupportsTools() const override { return true; }
        std::string GetName() const override { return "LocalLlama"; }

    private:
        bool RefreshFromPidFile();

        std::string endpoint_ = "http://127.0.0.1:8765";
        std::string model_;
        std::string pidFilePath_ = "external/llm/run/server.pid";
        bool autoDiscoverPid_ = true;
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
            ReadModelSetting(config, model_);
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

    FChatResponse FGeminiProvider::Chat(const FChatRequest& request)
    {
        if (!configured_)
        {
            return FChatResponse::Failure("Gemini provider not configured");
        }
        FChatRequest req = request;
        if (req.model.empty()) req.model = model_;
        if (req.maxTokens <= 0) req.maxTokens = AIConfig::MaxOutputTokens;

        std::string url = fmt::format("{}/models/{}:generateContent?key={}",
            endpoint_, req.model, apiKey_);
        json body = BuildGeminiChatRequestBody(req);
        FHttpResult http = HttpPostJson(url, body.dump());
        if (!http.ok)
        {
            return FChatResponse::Failure(http.error);
        }
        try
        {
            return ParseGeminiChatResponse(json::parse(http.body));
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse Gemini response: {}", e.what());
            return FChatResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    FAIResponse FGeminiProvider::Generate(const std::string& prompt)
    {
        FChatRequest req;
        req.messages.push_back(FChatMessage::User(prompt));
        return ChatToLegacyResponse(Chat(req));
    }

    bool FOllamaProvider::Initialize(const nlohmann::json& config)
    {
        try
        {
            if (config.contains("endpoint"))
            {
                endpoint_ = config["endpoint"].get<std::string>();
            }
            ReadModelSetting(config, model_);

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

    FChatResponse FOllamaProvider::Chat(const FChatRequest& request)
    {
        if (!configured_)
        {
            return FChatResponse::Failure("Ollama provider not configured");
        }
        FChatRequest req = request;
        if (req.model.empty()) req.model = model_;

        // If tools are requested, use Ollama's OpenAI-compatible /v1/chat/completions endpoint.
        // Otherwise stick with the legacy /api/generate path for backward compatibility.
        const bool useToolsPath = !req.tools.empty();
        const std::string url = useToolsPath
            ? endpoint_ + "/v1/chat/completions"
            : endpoint_ + "/api/generate";
        const json body = useToolsPath
            ? BuildOpenAIChatRequestBody(req)
            : BuildOllamaGenerateRequestBody(req);

        FHttpResult http = HttpPostJson(url, body.dump());
        if (!http.ok)
        {
            return FChatResponse::Failure(http.error);
        }
        try
        {
            json parsed = json::parse(http.body);
            return useToolsPath ? ParseOpenAIChatResponse(parsed) : ParseOllamaGenerateResponse(parsed);
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse Ollama response: {}", e.what());
            return FChatResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    FAIResponse FOllamaProvider::Generate(const std::string& prompt)
    {
        FChatRequest req;
        req.messages.push_back(FChatMessage::User(prompt));
        return ChatToLegacyResponse(Chat(req));
    }

    bool FZhipuProvider::Initialize(const nlohmann::json& config)
    {
        try
        {
            if (config.contains("apiKey"))
            {
                apiKey_ = config["apiKey"].get<std::string>();
            }
            ReadModelSetting(config, model_);
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

    FChatResponse FZhipuProvider::Chat(const FChatRequest& request)
    {
        if (!configured_)
        {
            return FChatResponse::Failure("Zhipu provider not configured");
        }
        FChatRequest req = request;
        if (req.model.empty()) req.model = model_;

        const std::string url = endpoint_ + "/chat/completions";
        const json body = BuildOpenAIChatRequestBody(req);
        std::vector<std::string> headers{fmt::format("Authorization: Bearer {}", apiKey_)};
        FHttpResult http = HttpPostJson(url, body.dump(), headers);
        if (!http.ok)
        {
            return FChatResponse::Failure(http.error);
        }
        try
        {
            return ParseOpenAIChatResponse(json::parse(http.body));
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse Zhipu response: {}", e.what());
            return FChatResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    FChatResponse FZhipuProvider::ChatStream(const FChatRequest& request, FChatStreamCallback onDelta)
    {
        if (!configured_)
        {
            return FChatResponse::Failure("Zhipu provider not configured");
        }
        FChatRequest req = request;
        if (req.model.empty()) req.model = model_;

        const std::string url = endpoint_ + "/chat/completions";
        json body = BuildOpenAIChatRequestBody(req);
        std::vector<std::string> headers{fmt::format("Authorization: Bearer {}", apiKey_)};
        return HttpPostOpenAIChatStream(url, std::move(body), headers, std::move(onDelta));
    }

    FAIResponse FZhipuProvider::Generate(const std::string& prompt)
    {
        FChatRequest req;
        req.messages.push_back(FChatMessage::User(prompt));
        return ChatToLegacyResponse(Chat(req));
    }

    bool FDeepSeekProvider::Initialize(const nlohmann::json& config)
    {
        try
        {
            if (config.contains("apiKey"))
            {
                apiKey_ = config["apiKey"].get<std::string>();
            }
            ReadModelSetting(config, model_);
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

    FChatResponse FDeepSeekProvider::Chat(const FChatRequest& request)
    {
        if (!configured_)
        {
            return FChatResponse::Failure("DeepSeek provider not configured");
        }
        FChatRequest req = request;
        if (req.model.empty()) req.model = model_;

        const std::string url = endpoint_ + "/chat/completions";
        const json body = BuildOpenAIChatRequestBody(req);
        std::vector<std::string> headers{fmt::format("Authorization: Bearer {}", apiKey_)};
        FHttpResult http = HttpPostJson(url, body.dump(), headers);
        if (!http.ok)
        {
            return FChatResponse::Failure(http.error);
        }
        try
        {
            return ParseOpenAIChatResponse(json::parse(http.body));
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse DeepSeek response: {}", e.what());
            return FChatResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    FChatResponse FDeepSeekProvider::ChatStream(const FChatRequest& request, FChatStreamCallback onDelta)
    {
        if (!configured_)
        {
            return FChatResponse::Failure("DeepSeek provider not configured");
        }
        FChatRequest req = request;
        if (req.model.empty()) req.model = model_;

        const std::string url = endpoint_ + "/chat/completions";
        json body = BuildOpenAIChatRequestBody(req);
        std::vector<std::string> headers{fmt::format("Authorization: Bearer {}", apiKey_)};
        return HttpPostOpenAIChatStream(url, std::move(body), headers, std::move(onDelta));
    }

    FAIResponse FDeepSeekProvider::Generate(const std::string& prompt)
    {
        FChatRequest req;
        req.messages.push_back(FChatMessage::User(prompt));
        return ChatToLegacyResponse(Chat(req));
    }

    bool FOpenAIProvider::Initialize(const nlohmann::json& config)
    {
        try
        {
            if (config.contains("apiKey") && config["apiKey"].is_string())
            {
                apiKey_ = config["apiKey"].get<std::string>();
            }
            ReadModelSetting(config, model_);
            if (config.contains("endpoint") && config["endpoint"].is_string())
            {
                endpoint_ = config["endpoint"].get<std::string>();
            }

            if (apiKey_.empty() || apiKey_ == "YOUR_OPENAI_API_KEY")
            {
                configured_ = false;
                return false;
            }

            configured_ = true;
            SPDLOG_INFO("OpenAI Provider initialized with model: {}", model_);
            return true;
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to initialize OpenAI Provider: {}", e.what());
            configured_ = false;
            return false;
        }
    }

    bool FOpenAIProvider::IsConfigured() const
    {
        return configured_;
    }

    FChatResponse FOpenAIProvider::Chat(const FChatRequest& request)
    {
        if (!configured_)
        {
            return FChatResponse::Failure("OpenAI provider not configured");
        }
        FChatRequest req = request;
        if (req.model.empty()) req.model = model_;

        const std::string url = endpoint_ + "/chat/completions";
        const json body = BuildOpenAIChatRequestBody(req);
        std::vector<std::string> headers{fmt::format("Authorization: Bearer {}", apiKey_)};
        FHttpResult http = HttpPostJson(url, body.dump(), headers);
        if (!http.ok)
        {
            return FChatResponse::Failure(http.error);
        }
        try
        {
            return ParseOpenAIChatResponse(json::parse(http.body));
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse OpenAI response: {}", e.what());
            return FChatResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    FChatResponse FOpenAIProvider::ChatStream(const FChatRequest& request, FChatStreamCallback onDelta)
    {
        if (!configured_)
        {
            return FChatResponse::Failure("OpenAI provider not configured");
        }
        FChatRequest req = request;
        if (req.model.empty()) req.model = model_;

        const std::string url = endpoint_ + "/chat/completions";
        json body = BuildOpenAIChatRequestBody(req);
        std::vector<std::string> headers{fmt::format("Authorization: Bearer {}", apiKey_)};
        return HttpPostOpenAIChatStream(url, std::move(body), headers, std::move(onDelta));
    }

    FAIResponse FOpenAIProvider::Generate(const std::string& prompt)
    {
        FChatRequest req;
        req.messages.push_back(FChatMessage::User(prompt));
        return ChatToLegacyResponse(Chat(req));
    }

    bool FLocalLlamaProvider::Initialize(const nlohmann::json& config)
    {
        try
        {
            if (config.contains("endpoint") && config["endpoint"].is_string())
            {
                endpoint_ = config["endpoint"].get<std::string>();
            }
            ReadModelSetting(config, model_);
            if (config.contains("pidFile") && config["pidFile"].is_string())
            {
                pidFilePath_ = config["pidFile"].get<std::string>();
            }
            if (config.contains("autoDiscoverPid") && config["autoDiscoverPid"].is_boolean())
            {
                autoDiscoverPid_ = config["autoDiscoverPid"].get<bool>();
            }

            if (autoDiscoverPid_)
            {
                RefreshFromPidFile();
            }

            // Configured if we have a usable endpoint; PID may fail (server not running),
            // but as long as a fallback endpoint is set we count it as configured.
            configured_ = !endpoint_.empty();
            if (configured_)
            {
                SPDLOG_INFO("LocalLlama Provider initialized: endpoint={} model={}",
                            endpoint_, model_.empty() ? "<auto>" : model_);
            }
            return configured_;
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to initialize LocalLlama Provider: {}", e.what());
            configured_ = false;
            return false;
        }
    }

    bool FLocalLlamaProvider::IsConfigured() const
    {
        return configured_;
    }

    bool FLocalLlamaProvider::RefreshFromPidFile()
    {
        std::string resolved = Utilities::FileHelper::GetPlatformFilePath(pidFilePath_.c_str());
        FLlamaPidInfo info = ReadLlamaPidFile(resolved);
        if (!info.valid)
        {
            return false;
        }
        endpoint_ = info.Endpoint();
        if (model_.empty())
        {
            model_ = info.model;
        }
        return true;
    }

    FChatResponse FLocalLlamaProvider::Chat(const FChatRequest& request)
    {
        if (!configured_)
        {
            return FChatResponse::Failure("LocalLlama provider not configured");
        }
        // Re-read PID each call so model swaps via `gnb llm serve --model <id>`
        // are picked up without restarting the engine.
        if (autoDiscoverPid_)
        {
            RefreshFromPidFile();
        }

        FChatRequest req = request;
        if (req.model.empty()) req.model = model_;

        const std::string url = endpoint_ + "/v1/chat/completions";
        const json body = BuildOpenAIChatRequestBody(req, /*injectThinkingControl=*/true);
        FHttpResult http = HttpPostJson(url, body.dump());
        if (!http.ok)
        {
            return FChatResponse::Failure(http.error);
        }
        try
        {
            return ParseOpenAIChatResponse(json::parse(http.body));
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse LocalLlama response: {}", e.what());
            return FChatResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    FChatResponse FLocalLlamaProvider::ChatStream(const FChatRequest& request, FChatStreamCallback onDelta)
    {
        if (!configured_)
        {
            return FChatResponse::Failure("LocalLlama provider not configured");
        }
        if (autoDiscoverPid_)
        {
            RefreshFromPidFile();
        }

        FChatRequest req = request;
        if (req.model.empty()) req.model = model_;

        const std::string url = endpoint_ + "/v1/chat/completions";
        json body = BuildOpenAIChatRequestBody(req, /*injectThinkingControl=*/true);
        return HttpPostOpenAIChatStream(url, std::move(body), {}, std::move(onDelta));
    }

    FAIResponse FLocalLlamaProvider::Generate(const std::string& prompt)
    {
        FChatRequest req;
        req.messages.push_back(FChatMessage::User(prompt));
        return ChatToLegacyResponse(Chat(req));
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
        case EAIProviderType::OpenAI:
            return std::make_unique<FOpenAIProvider>();
        case EAIProviderType::LocalLlama:
            return std::make_unique<FLocalLlamaProvider>();
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
        case EAIProviderType::OpenAI: return "openai";
        case EAIProviderType::LocalLlama: return "localllm";
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
        if (lower == "openai" || lower == "openai-compatible") return EAIProviderType::OpenAI;
        if (lower == "localllm" || lower == "local" || lower == "llama") return EAIProviderType::LocalLlama;
        return EAIProviderType::Gemini;
    }

    std::vector<std::pair<EAIProviderType, std::string>> FAIService::GetAvailableProviders()
    {
        return {
            {EAIProviderType::LocalLlama, "LocalLlama"},
            {EAIProviderType::Gemini, "Gemini"},
            {EAIProviderType::Ollama, "Ollama"},
            {EAIProviderType::Zhipu, "Zhipu"},
            {EAIProviderType::DeepSeek, "DeepSeek"},
            {EAIProviderType::OpenAI, "OpenAI"}
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

    std::vector<std::string> FAIService::GetProviderModels(EAIProviderType type) const
    {
        auto it = providerModels_.find(type);
        if (it != providerModels_.end())
        {
            return it->second;
        }

        std::string fallback = GetProviderDefaultModel(type);
        if (!fallback.empty())
        {
            return {fallback};
        }
        return {};
    }

    std::string FAIService::GetProviderDefaultModel(EAIProviderType type) const
    {
        nlohmann::json config = GetProviderConfig(type);
        if (config.contains("defaultModel") && config["defaultModel"].is_string())
        {
            return config["defaultModel"].get<std::string>();
        }
        if (config.contains("model") && config["model"].is_string())
        {
            return config["model"].get<std::string>();
        }
        return "";
    }

    std::string FAIService::GetCurrentModel() const
    {
        auto selected = providerModelSelection_.find(providerType_);
        if (selected != providerModelSelection_.end() && !selected->second.empty())
        {
            return selected->second;
        }
        return GetProviderDefaultModel(providerType_);
    }

    bool FAIService::SetCurrentModel(std::string model)
    {
        if (model.empty())
        {
            return false;
        }

        providerModelSelection_[providerType_] = std::move(model);
        return true;
    }

    void FAIService::UpdateProviderConfigCache()
    {
        providerConfigCache_.clear();
        providerModels_.clear();

        for (const auto& [type, name] : GetAvailableProviders())
        {
            auto tempProvider = CreateProvider(type);
            if (tempProvider)
            {
                nlohmann::json config = GetProviderConfig(type);
                std::vector<std::string> models;
                if (config.contains("models") && config["models"].is_array())
                {
                    for (const auto& item : config["models"])
                    {
                        if (item.is_string())
                        {
                            models.push_back(item.get<std::string>());
                        }
                    }
                }
                if (models.empty())
                {
                    std::string defaultModel = GetProviderDefaultModel(type);
                    if (!defaultModel.empty())
                    {
                        models.push_back(defaultModel);
                    }
                }
                providerModels_[type] = std::move(models);

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

            std::vector<std::filesystem::path> secretCandidates;
            if (j.contains("secretsPath") && j["secretsPath"].is_string())
            {
                secretCandidates.push_back(ResolveConfigRelativePath(j["secretsPath"].get<std::string>()));
            }
            if (const char* envPath = std::getenv("GKNEXT_AI_SECRETS"))
            {
                if (envPath[0] != '\0')
                {
                    secretCandidates.emplace_back(envPath);
                }
            }
            if (const char* localAppData = std::getenv("LOCALAPPDATA"))
            {
                if (localAppData[0] != '\0')
                {
                    secretCandidates.push_back(
                        std::filesystem::path(localAppData) / "gkNextEngine" / "ai_secrets.json");
                }
            }
            if (const char* appData = std::getenv("APPDATA"))
            {
                if (appData[0] != '\0')
                {
                    secretCandidates.push_back(
                        std::filesystem::path(appData) / "gkNextRenderer" / "gkNextEngine" / "ai_secrets.json");
                }
            }
            secretCandidates.push_back(NextPlatform::UserPaths::GetUserDataDir("gkNextEngine") / "ai_secrets.json");

            for (const std::filesystem::path& secretPath : secretCandidates)
            {
                std::error_code ec;
                if (!std::filesystem::exists(secretPath, ec))
                {
                    continue;
                }

                try
                {
                    json secrets;
                    if (TryReadJsonFile(secretPath, secrets))
                    {
                        MergeSecretObject(j, secrets);
                        SPDLOG_INFO("AI secrets loaded from {}", secretPath.string());
                        break;
                    }
                }
                catch (const std::exception& e)
                {
                    SPDLOG_WARN("Failed to read AI secrets from {}: {}", secretPath.string(), e.what());
                }
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

        FChatRequest req;
        req.messages.push_back(FChatMessage::User(prompt));
        std::string selectedModel = GetCurrentModel();
        if (!selectedModel.empty())
        {
            req.model = std::move(selectedModel);
        }
        return ChatToLegacyResponse(provider_->Chat(req));
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

    FChatResponse FAIService::Chat(const FChatRequest& request)
    {
        if (!configured_ || !provider_)
        {
            return FChatResponse::Failure("AI service not configured");
        }

        status_ = EAIStatus::Generating;
        statusMessage_ = "Generating...";
        FChatRequest req = request;
        if (req.model.empty())
        {
            req.model = GetCurrentModel();
        }
        FChatResponse response = provider_->Chat(req);
        if (response.success)
        {
            status_ = EAIStatus::Ready;
            statusMessage_ = "Ready";
        }
        else
        {
            status_ = EAIStatus::Error;
            statusMessage_ = response.errorMessage;
        }
        return response;
    }

    FChatResponse FAIService::ChatStream(const FChatRequest& request, FChatStreamCallback onDelta)
    {
        if (!configured_ || !provider_)
        {
            return FChatResponse::Failure("AI service not configured");
        }

        status_ = EAIStatus::Generating;
        statusMessage_ = "Generating...";
        FChatRequest req = request;
        if (req.model.empty())
        {
            req.model = GetCurrentModel();
        }
        FChatResponse response = provider_->ChatStream(req, std::move(onDelta));
        if (response.success)
        {
            status_ = EAIStatus::Ready;
            statusMessage_ = "Ready";
        }
        else
        {
            status_ = EAIStatus::Error;
            statusMessage_ = response.errorMessage;
        }
        return response;
    }

    bool FAIService::SupportsTools() const
    {
        return provider_ && provider_->SupportsTools();
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
