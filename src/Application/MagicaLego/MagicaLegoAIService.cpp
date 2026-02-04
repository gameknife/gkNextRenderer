#include "MagicaLegoAIService.hpp"
#include "MagicaLegoGameInstance.hpp"
#include "Utilities/FileHelper.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>

using json = nlohmann::json;

namespace
{
    size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        static_cast<std::string*>(userp)->append(static_cast<char*>(contents), size * nmemb);
        return size * nmemb;
    }
}

namespace MagicaLego
{
    FAIService::FAIService(MagicaLegoGameInstance* gi)
        : gameInstance_(gi)
    {
        LoadConfig();
    }

    bool FAIService::LoadConfig()
    {
        std::string configPath = Utilities::FileHelper::GetPlatformFilePath("assets/configs/ai_config.json");
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

            if (j.contains("apiKey"))
            {
                config_.apiKey = j["apiKey"].get<std::string>();
            }
            if (j.contains("model"))
            {
                config_.model = j["model"].get<std::string>();
            }
            if (j.contains("endpoint"))
            {
                config_.endpoint = j["endpoint"].get<std::string>();
            }

            if (config_.apiKey.empty() || config_.apiKey == "YOUR_GOOGLE_API_KEY")
            {
                status_ = EAIStatus::NotConfigured;
                statusMessage_ = "API key not configured";
                configured_ = false;
                return false;
            }

            status_ = EAIStatus::Ready;
            statusMessage_ = "Ready";
            configured_ = true;
            SPDLOG_INFO("AI Service configured with model: {}", config_.model);
            return true;
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

    std::string FAIService::BuildSystemPrompt()
    {
        std::string prompt = R"(You are a MagicaLego building assistant. Generate mlscript scripts based on user descriptions.

## Available Commands
- place <Type>/<Color> <x> <y> <z> [orientation]
- dig <x> <y> <z>

## Available Block Types
)";

        // Add block types from game instance
        if (gameInstance_)
        {
            auto types = gameInstance_->GetAllBlockTypes();
            for (const auto& type : types)
            {
                prompt += fmt::format("- {}\n", type);
            }
        }

        prompt += R"(
## Available Colors
Colors are referenced by index: #0, #1, #2, etc.
)";

        // Add color info for first type
        if (gameInstance_)
        {
            auto types = gameInstance_->GetAllBlockTypes();
            if (!types.empty())
            {
                auto colors = gameInstance_->GetAllBlockColors(types[0]);
                prompt += fmt::format("Available colors for {}: ", types[0]);
                for (size_t i = 0; i < colors.size() && i < 20; ++i)
                {
                    prompt += fmt::format("#{} ({}), ", i, colors[i]);
                }
                prompt += "...\n";
            }
        }

        prompt += R"(
## Script Syntax
- Full line comments: # text
- Inline comments: command # comment
- Variables: var name = value
- Loops: repeat count as i ... end (i goes from 0 to count-1)
- Variable reference: $varName
- Expressions: $(varName + number) or $(varName - number)

## Examples
```mlscript
# Build a 3x3 floor
repeat 3 as x
    repeat 3 as z
        place Plate1x1/#1 $(x - 1) 0 $(z - 1)
    end
end

# Build a wall
repeat 5 as i
    place Block1x1/#0 $i 0 0  # bottom row
    place Block1x1/#0 $i 1 0  # top row
end
```

## Rules
1. Output ONLY mlscript code, no explanations or markdown
2. Use loops to reduce repetitive code
3. y=0 is ground level, y increases upward
4. Coordinate origin (0,0,0) is at center
5. Loop iterator starts from 0, use $(i - offset) to center
6. Orientations: north, east, south, west

User request: )";

        return prompt;
    }

    FAIResponse FAIService::CallGeminiAPI(const std::string& userPrompt)
    {
        if (!configured_)
        {
            return FAIResponse::Failure("AI service not configured");
        }

        std::string systemPrompt = BuildSystemPrompt();
        std::string fullPrompt = systemPrompt + userPrompt;

        // Build API URL
        std::string url = fmt::format("{}/models/{}:generateContent?key={}",
            config_.endpoint, config_.model, config_.apiKey);

        // Build request body
        json requestBody = {
            {"contents", json::array({
                {{"role", "user"}, {"parts", json::array({{{"text", fullPrompt}}})}}
            })},
            {"generationConfig", {
                {"temperature", 0.7},
                {"maxOutputTokens", 2048}
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
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBuffer);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

        CURLcode res = curl_easy_perform(curl);

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res != CURLE_OK)
        {
            return FAIResponse::Failure(fmt::format("Network error: {}", curl_easy_strerror(res)));
        }

        // Parse response
        try
        {
            json response = json::parse(responseBuffer);

            // Check for API error
            if (response.contains("error"))
            {
                std::string errorMsg = response["error"]["message"].get<std::string>();
                return FAIResponse::Failure(fmt::format("API error: {}", errorMsg));
            }

            // Extract generated text
            if (response.contains("candidates") && !response["candidates"].empty())
            {
                auto& candidate = response["candidates"][0];
                if (candidate.contains("content") && candidate["content"].contains("parts"))
                {
                    auto& parts = candidate["content"]["parts"];
                    if (!parts.empty() && parts[0].contains("text"))
                    {
                        std::string generatedText = parts[0]["text"].get<std::string>();
                        std::string script = ExtractScriptFromResponse(generatedText);
                        return FAIResponse::Success(script);
                    }
                }
            }

            return FAIResponse::Failure("Unexpected API response format");
        }
        catch (const std::exception& e)
        {
            SPDLOG_ERROR("Failed to parse AI response: {}", e.what());
            return FAIResponse::Failure(fmt::format("Response parse error: {}", e.what()));
        }
    }

    std::string FAIService::ExtractScriptFromResponse(const std::string& responseText)
    {
        std::string result = responseText;

        // Try to extract code from markdown code block
        size_t codeStart = result.find("```mlscript");
        if (codeStart == std::string::npos)
        {
            codeStart = result.find("```");
        }

        if (codeStart != std::string::npos)
        {
            size_t contentStart = result.find('\n', codeStart);
            if (contentStart != std::string::npos)
            {
                contentStart++;
                size_t codeEnd = result.find("```", contentStart);
                if (codeEnd != std::string::npos)
                {
                    result = result.substr(contentStart, codeEnd - contentStart);
                }
                else
                {
                    result = result.substr(contentStart);
                }
            }
        }

        // Trim whitespace
        auto start = result.find_first_not_of(" \t\r\n");
        auto end = result.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos)
        {
            result = result.substr(start, end - start + 1);
        }

        return result;
    }

    FAIResponse FAIService::GenerateScript(const std::string& prompt)
    {
        status_ = EAIStatus::Generating;
        statusMessage_ = "Generating...";

        auto response = CallGeminiAPI(prompt);

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

    void FAIService::GenerateScriptAsync(const std::string& prompt,
                                         std::function<void(FAIResponse)> callback)
    {
        status_ = EAIStatus::Generating;
        statusMessage_ = "Generating...";

        std::thread([this, prompt, callback]()
        {
            auto response = CallGeminiAPI(prompt);

            {
                std::lock_guard<std::mutex> lock(resultMutex_);
                pendingResult_ = response;
                hasPendingResult_ = true;
            }

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

    FAIResponse FAIService::GetPendingResult()
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        hasPendingResult_ = false;
        return pendingResult_;
    }
}
