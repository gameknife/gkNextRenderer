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
        std::string prompt = R"(You are a MagicaLego building assistant. You control a virtual cursor to place blocks. Generate mlscript scripts based on user descriptions.

## Coordinate System (IMPORTANT)
- X axis: West(-) to East(+)
- Y axis: Down(-) to Up(+), y=0 is ground level
- Z axis: North(-) to South(+)
- Origin (0,0,0) is at the center of the building area

**Direction to Axis Mapping:**
| Direction | Axis  | Movement        |
|-----------|-------|-----------------|
| North     | -Z    | Z decreases     |
| South     | +Z    | Z increases     |
| East      | +X    | X increases     |
| West      | -X    | X decreases     |

## Cursor System
- Cursor starts at position (0,0,0), initially facing North (-Z direction)
- The cursor tracks your current building position
- **IMPORTANT**: When mixing relative commands (move/place here) with absolute coordinates (place x y z), ensure they target the same area!
- If placing objects at positive X,Z coordinates, consider using `face east` or `face south` as starting direction

## Available Commands

### Block Placement
- place <Type>/<Color> <x> <y> <z> [orientation]  # Absolute coordinates
- place <Type>/<Color> here [orientation]         # At cursor position
- place <Type>/<Color> ahead [n] [orientation]    # n steps in front (default 1)
- dig <x> <y> <z>                                 # Remove block

### Cursor Control
- move forward [n]    # Move cursor forward n steps (default 1)
- move backward [n]   # Move backward
- move left [n]       # Move left
- move right [n]      # Move right
- move up [n]         # Move up
- move down [n]       # Move down
- turn left           # Turn 90° left
- turn right          # Turn 90° right
- turn around         # Turn 180°
- goto <x> <y> <z>    # Move to absolute position
- face <north|east|south|west>  # Set facing direction

### Environment
- scan [radius]       # Scan blocks around cursor (returns JSON)

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
## Block Height Properties (IMPORTANT for layering)
- **Flat types** (Flat1x1, Plate1x1, Plate2x2, Button1x1): These are thin/flat blocks that can be COVERED by other blocks at the same position
- **Full height types** (Block1x1, Cylinder1x1, Slope1x2, Corner2x2): These are taller blocks that cover flat blocks

**PLACEMENT ORDER RULE:**
When placing blocks at the same (x, y, z) position, ALWAYS place flat/thin blocks FIRST, then place full-height blocks.
Example: To put a Block on top of a Plate at position (0,0,0):
1. First: place Plate1x1/#0 0 0 0
2. Then: place Block1x1/#1 0 0 0
This ensures correct visual layering (Block covers the Plate).

## Block Height Properties (IMPORTANT for layering)
- **Flat types** (Flat1x1, Plate1x1, Plate2x2, Button1x1): These are thin/flat blocks that can be COVERED by
other blocks at the same position
- **Full height types** (Block1x1, Cylinder1x1, Slope1x2, Corner2x2): These are taller blocks that cover flat
blocks

**PLACEMENT ORDER RULE:**
When placing blocks at the same (x, y, z) position, ALWAYS place flat/thin blocks FIRST, then place
full-height blocks.
Example: To put a Block on top of a Plate at position (0,0,0):
1. First: place Plate1x1/#0 0 0 0
2. Then: place Block1x1/#1 0 0 0
This ensures correct visual layering (Block covers the Plate).

## Available Colors (STRICT - use ONLY these exact values)
)";

        // Add actual color values from game instance
        if (gameInstance_)
        {
            auto types = gameInstance_->GetAllBlockTypes();
            if (!types.empty())
            {
                auto colors = gameInstance_->GetAllBlockColors(types[0]);

                prompt += fmt::format("**ONLY {} colors exist. You MUST use one of these exact values:**\n", colors.size());
                for (const auto& color : colors)
                {
                    prompt += fmt::format("{}, ", color);
                }
                prompt += "\n\n";
            }
        }

        prompt += R"(**CRITICAL COLOR RULES:**
- ONLY use color values from the list above (copy them exactly)
- DO NOT use any color value not in the list
- DO NOT invent or guess color values
- If unsure, use the first color from the list
)";

        prompt += R"(
## Script Syntax (STRICT - follow exactly)
- Comments: # text (full line only)
- Variables: var name = value
- Loops: MUST use this exact format:
  repeat <count> as <var>
      <commands>
  end
- Variable reference: $varName
- Expressions: $(varName + number) or $(varName - number)

## CRITICAL RULES for repeat/end:
- Every "repeat" MUST have a matching "end" on its own line
- "end" must be lowercase and on a separate line
- Nested loops need multiple "end" statements (one per repeat)
- Do NOT use curly braces {} or other syntax

## Examples

### Method 1: Floor with cursor (face east for positive X/Z area)
# Build a 3x3 floor covering (0,0,0) to (2,0,2)
goto 0 0 0
face east            # forward=+X, right=+Z
repeat 3 as row
    repeat 3 as col
        place Plate1x1/#1 here
        move forward     # +X
    end
    move backward 3      # back to X=0
    move right           # +Z
end

### Method 2: Using absolute coordinates
# Build a 3x3 floor at (-1,0,-1) to (1,0,1)
repeat 3 as x
    repeat 3 as z
        place Plate1x1/#1 $(x - 1) 0 $(z - 1)
    end
end

### Tower example
# 5-block tall tower at origin
repeat 5 as y
    place Block1x1/#0 0 $y 0
end

### L-shaped wall using cursor
goto 0 0 0
face east
repeat 4 as i
    place Block1x1/#0 here
    move forward
end
turn right           # now facing south (+Z)
repeat 3 as i
    place Block1x1/#0 here
    move forward
end

### Forest example (floor + trees)
# Floor at (0,0,0) to (9,0,9), then tree at (2,y,2)
goto 0 0 0
face east            # IMPORTANT: use east to build in +X/+Z area
repeat 10 as row
    repeat 10 as col
        place Plate1x1/#119 here
        move forward
    end
    move backward 10
    move right
end
# Tree trunk at absolute position (within the floor area)
repeat 4 as h
    place Block1x1/#192 2 $h 2
end

## Output Rules:
1. Output ONLY the script code, no markdown, no explanations
2. y=0 is ground, y increases upward
3. Origin (0,0,0) is at center
4. Always match repeat with end
5. Prefer cursor commands for connected/sequential builds
6. Use absolute coordinates for scattered/random placement
7. **CRITICAL**: When mixing cursor movement with absolute coordinates, ensure the cursor-built area contains your absolute positions (e.g., face east to build in +X/+Z quadrant if placing objects at positive coordinates)

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
                {"maxOutputTokens", 819200}
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
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

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
