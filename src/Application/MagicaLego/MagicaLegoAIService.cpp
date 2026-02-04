#include "MagicaLegoAIService.hpp"
#include "MagicaLegoGameInstance.hpp"
#include "MagicaLegoConstants.hpp"
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

    // Convert RGB to HSL for color analysis
    void RgbToHsl(float r, float g, float b, float& h, float& s, float& l)
    {
        float maxC = std::max({r, g, b});
        float minC = std::min({r, g, b});
        l = (maxC + minC) / 2.0f;

        if (maxC == minC)
        {
            h = s = 0.0f;
        }
        else
        {
            float d = maxC - minC;
            s = l > 0.5f ? d / (2.0f - maxC - minC) : d / (maxC + minC);

            if (maxC == r)
                h = (g - b) / d + (g < b ? 6.0f : 0.0f);
            else if (maxC == g)
                h = (b - r) / d + 2.0f;
            else
                h = (r - g) / d + 4.0f;

            h /= 6.0f;
        }
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

    FColorSemantic FAIService::AnalyzeColor(const std::string& colorCode, glm::vec4 rgba)
    {
        FColorSemantic result;
        result.colorCode = colorCode;

        float h, s, l;
        RgbToHsl(rgba.r, rgba.g, rgba.b, h, s, l);

        // Convert hue to degrees for easier reasoning
        float hueDeg = h * 360.0f;

        // Determine color name and category based on HSL
        if (l < 0.15f)
        {
            result.colorName = "black";
            result.category = "neutral";
            result.suggestedUse = "outlines, shadows, details";
        }
        else if (l > 0.85f)
        {
            result.colorName = "white";
            result.category = "neutral";
            result.suggestedUse = "snow, clouds, highlights";
        }
        else if (s < 0.15f)
        {
            // Grayscale
            if (l < 0.4f)
            {
                result.colorName = "dark gray";
                result.category = "neutral";
                result.suggestedUse = "stone, metal, concrete";
            }
            else if (l < 0.7f)
            {
                result.colorName = "gray";
                result.category = "neutral";
                result.suggestedUse = "stone, pavement, metal";
            }
            else
            {
                result.colorName = "light gray";
                result.category = "neutral";
                result.suggestedUse = "clouds, light stone";
            }
        }
        else
        {
            // Chromatic colors - analyze by hue
            if (hueDeg < 15 || hueDeg >= 345)
            {
                result.colorName = l < 0.4f ? "dark red" : (l > 0.7f ? "pink" : "red");
                result.category = "accent";
                result.suggestedUse = "flowers, fruit, roofs, decoration";
            }
            else if (hueDeg < 45)
            {
                if (s < 0.5f && l < 0.5f)
                {
                    result.colorName = "brown";
                    result.category = "nature";
                    result.suggestedUse = "tree trunk, wood, earth, dirt";
                }
                else
                {
                    result.colorName = l < 0.5f ? "dark orange" : "orange";
                    result.category = "accent";
                    result.suggestedUse = "fruit, autumn leaves, decoration";
                }
            }
            else if (hueDeg < 70)
            {
                if (s < 0.5f && l < 0.5f)
                {
                    result.colorName = "tan/beige";
                    result.category = "nature";
                    result.suggestedUse = "sand, wood, path";
                }
                else
                {
                    result.colorName = l < 0.5f ? "dark yellow" : "yellow";
                    result.category = "accent";
                    result.suggestedUse = "flowers, sun, gold, decoration";
                }
            }
            else if (hueDeg < 160)
            {
                // Green range
                if (l < 0.35f)
                {
                    result.colorName = "dark green";
                    result.category = "nature";
                    result.suggestedUse = "tree leaves, bushes, forest";
                }
                else if (l > 0.6f)
                {
                    result.colorName = "light green";
                    result.category = "nature";
                    result.suggestedUse = "grass, young leaves, spring foliage";
                }
                else
                {
                    result.colorName = "green";
                    result.category = "nature";
                    result.suggestedUse = "grass, leaves, foliage, plants";
                }
            }
            else if (hueDeg < 200)
            {
                result.colorName = "cyan/teal";
                result.category = "accent";
                result.suggestedUse = "water, ice, decoration";
            }
            else if (hueDeg < 260)
            {
                if (l > 0.6f)
                {
                    result.colorName = "light blue";
                    result.category = "nature";
                    result.suggestedUse = "sky, water, windows";
                }
                else
                {
                    result.colorName = l < 0.4f ? "dark blue" : "blue";
                    result.category = "building";
                    result.suggestedUse = "water, windows, decoration";
                }
            }
            else if (hueDeg < 300)
            {
                result.colorName = l < 0.5f ? "purple" : "violet";
                result.category = "accent";
                result.suggestedUse = "flowers, magic, decoration";
            }
            else
            {
                result.colorName = l < 0.5f ? "magenta" : "pink";
                result.category = "accent";
                result.suggestedUse = "flowers, decoration";
            }
        }

        return result;
    }

    std::string FAIService::BuildColorVocabulary()
    {
        if (!gameInstance_)
        {
            return "";
        }

        std::string vocabulary = R"(
## Color Vocabulary (Natural Language → Color Codes)
Use these semantic descriptions to choose appropriate colors:

)";

        // Collect all colors with their semantic info
        std::map<std::string, std::vector<FColorSemantic>> byCategory;

        auto& library = gameInstance_->GetBasicNodeLibrary();
        for (const auto& [typeName, blocks] : library)
        {
            for (const auto& block : blocks)
            {
                auto semantic = AnalyzeColor(block.name, block.color);
                byCategory[semantic.category].push_back(semantic);
            }
            break; // Only need colors from one type (they're shared)
        }

        // Output by category
        const std::vector<std::pair<std::string, std::string>> categoryOrder = {
            {"nature", "### Nature Colors (grass, trees, earth)"},
            {"neutral", "### Neutral Colors (stone, metal, concrete)"},
            {"building", "### Building Colors (walls, roofs)"},
            {"accent", "### Accent Colors (flowers, fruit, decoration)"}
        };

        for (const auto& [cat, header] : categoryOrder)
        {
            auto it = byCategory.find(cat);
            if (it == byCategory.end() || it->second.empty())
                continue;

            vocabulary += header + "\n";
            for (const auto& sem : it->second)
            {
                vocabulary += fmt::format("- **{}** ({}): {}\n",
                    sem.colorName, sem.colorCode, sem.suggestedUse);
            }
            vocabulary += "\n";
        }

        vocabulary += R"(**Usage Example:**
- "grass" → use Flat1x1 or Plate1x1 with a green color from Nature Colors
- "tree trunk" → use Block1x1 with brown from Nature Colors
- "red fruit" → use Button1x1 with red from Accent Colors
- "stone wall" → use Block1x1 with gray from Neutral Colors

)";

        return vocabulary;
    }

    std::vector<FColorSemantic> FAIService::GetColorSemantics()
    {
        std::vector<FColorSemantic> result;

        if (!gameInstance_)
        {
            return result;
        }

        auto& library = gameInstance_->GetBasicNodeLibrary();
        for (const auto& [typeName, blocks] : library)
        {
            for (const auto& block : blocks)
            {
                result.push_back(AnalyzeColor(block.name, block.color));
            }
            break; // Only need colors from one type (they're shared)
        }

        // Sort by category then by color name
        std::sort(result.begin(), result.end(), [](const FColorSemantic& a, const FColorSemantic& b) {
            if (a.category != b.category) return a.category < b.category;
            return a.colorName < b.colorName;
        });

        return result;
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
- **Flat types** (Flat1x1, Plate1x1, Plate2x2, Button1x1): Thin/flat blocks that can be COVERED by other blocks
- **Full height types** (Block1x1, Cylinder1x1, Slope1x2, Corner2x2): Taller blocks that cover flat blocks

**PLACEMENT ORDER RULE:** When placing at same position, place flat blocks FIRST, then full-height blocks.
)";

        // Add color vocabulary (semantic descriptions)
        prompt += BuildColorVocabulary();

        prompt += R"(**CRITICAL COLOR RULES:**
- ONLY use color codes shown above (e.g., #119, #28, #192)
- Choose colors based on their semantic description (e.g., "brown" for tree trunks)
- DO NOT invent color codes not in the vocabulary
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

    std::string FAIService::BuildContextPrompt(const std::string& userPrompt)
    {
        std::string contextPrompt;

        // Get current scene description
        if (gameInstance_)
        {
            std::string sceneDesc = gameInstance_->GetCurrentSceneDescription();

            contextPrompt = R"(
## CURRENT SCENE CONTEXT
The user has already built something. Your task is to ADD to or MODIFY the existing scene based on their request.
DO NOT rebuild what already exists unless specifically asked to replace it.

)";
            contextPrompt += sceneDesc;
            contextPrompt += R"(

## YOUR TASK
Based on the existing scene above, generate ADDITIONAL script to fulfill the user's request.
- Use coordinates that don't overlap with existing blocks (unless replacing)
- Consider the existing bounding box when placing new elements
- Maintain visual consistency with what's already built

)";
        }

        contextPrompt += userPrompt;
        return contextPrompt;
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
                {"maxOutputTokens", AI::MaxOutputTokens}
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
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, AI::RequestTimeoutSeconds);

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

    FAIResponse FAIService::GenerateScriptWithContext(const std::string& prompt)
    {
        status_ = EAIStatus::Generating;
        statusMessage_ = "Generating with context...";

        // Build prompt with scene context
        std::string contextPrompt = BuildContextPrompt(prompt);
        auto response = CallGeminiAPI(contextPrompt);

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

    void FAIService::GenerateScriptWithContextAsync(const std::string& prompt,
                                                    std::function<void(FAIResponse)> callback)
    {
        status_ = EAIStatus::Generating;
        statusMessage_ = "Generating with context...";

        std::thread([this, prompt, callback]()
        {
            std::string contextPrompt = BuildContextPrompt(prompt);
            auto response = CallGeminiAPI(contextPrompt);

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
