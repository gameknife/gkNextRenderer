#pragma once

#include "AirportSimTypes.h"

#include <algorithm>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace AirportSim
{
    inline constexpr std::string_view kStructuredDecisionSchema = R"json({
        "type":"object","additionalProperties":false,
        "required":["action","target","say","mood"],
        "properties":{
            "action":{"type":"string","enum":["idle","goto","use_poi","say_to"]},
            "target":{"type":"string","maxLength":64},
            "say":{"type":"string","maxLength":20},
            "mood":{"type":"string","enum":["neutral","happy","tired","annoyed","excited","anxious"]}
        }
    })json";

    inline bool IsAllowedStructuredDecisionAction(std::string_view action)
    {
        return action == "idle" || action == "goto" || action == "use_poi" || action == "say_to";
    }

    inline bool IsAllowedStructuredDecisionMood(std::string_view mood)
    {
        return mood == "neutral" || mood == "happy" || mood == "tired" || mood == "annoyed" ||
               mood == "excited" || mood == "anxious";
    }

    inline size_t Utf8CodePointCount(std::string_view text)
    {
        return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](unsigned char byte)
        {
            return (byte & 0xC0U) != 0x80U;
        }));
    }

    inline FDecisionResult ParseStructuredDecision(const std::string& text)
    {
        FDecisionResult result;
        const size_t open = text.find('{');
        const size_t close = text.rfind('}');
        if (open == std::string::npos || close == std::string::npos || close <= open)
        {
            return result;
        }
        try
        {
            const nlohmann::json json = nlohmann::json::parse(text.substr(open, close - open + 1));
            result.action = json.value("action", std::string("idle"));
            result.target = json.value("target", std::string());
            result.say = json.value("say", std::string());
            const std::string mood = json.value("mood", std::string("neutral"));
            if (!IsAllowedStructuredDecisionAction(result.action) || Utf8CodePointCount(result.target) > 64 ||
                Utf8CodePointCount(result.say) > 20 || !IsAllowedStructuredDecisionMood(mood))
            {
                return FDecisionResult{};
            }
            result.mood = MoodFromString(mood);
            result.valid = true;
        }
        catch (...)
        {
            result.valid = false;
        }
        return result;
    }
}
