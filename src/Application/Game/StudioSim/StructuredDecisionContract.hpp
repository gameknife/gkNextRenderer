#pragma once

#include "StudioSimTypes.h"

#include <algorithm>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace StudioSim
{
    inline constexpr std::string_view kStructuredDecisionSchema = R"json({
        "type":"object","additionalProperties":false,
        "required":["action","target_poi","target_employee","dialogue","mood","duration_minutes"],
        "properties":{
            "action":{"type":"string","enum":["WORK","REST","TALK","MEETING","IDLE"]},
            "target_poi":{"type":"string","maxLength":64},
            "target_employee":{"type":"string","maxLength":64},
            "dialogue":{"type":"string","maxLength":15},
            "mood":{"type":"string","enum":["calm","focused","stressed","excited","bored","panicked"]},
            "duration_minutes":{"type":"integer","minimum":10,"maximum":60}
        }
    })json";

    inline bool IsAllowedStructuredDecisionAction(std::string_view action)
    {
        return action == "WORK" || action == "REST" || action == "TALK" || action == "MEETING" ||
               action == "IDLE";
    }

    inline bool IsAllowedStructuredDecisionMood(std::string_view mood)
    {
        return mood == "calm" || mood == "focused" || mood == "stressed" || mood == "excited" ||
               mood == "bored" || mood == "panicked";
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
            result.action = json.value("action", std::string("IDLE"));
            result.targetPoi = json.value("target_poi", std::string());
            result.targetEmployee = json.value("target_employee", std::string());
            result.dialogue = json.value("dialogue", std::string());
            const std::string mood = json.value("mood", std::string("calm"));
            if (!IsAllowedStructuredDecisionAction(result.action) || Utf8CodePointCount(result.targetPoi) > 64 ||
                Utf8CodePointCount(result.targetEmployee) > 64 || Utf8CodePointCount(result.dialogue) > 15 ||
                !IsAllowedStructuredDecisionMood(mood))
            {
                return FDecisionResult{};
            }
            result.mood = MoodFromString(mood);
            result.durationMinutes = std::clamp(json.value("duration_minutes", 30), 10, 60);
            result.valid = true;
        }
        catch (...)
        {
            result.valid = false;
        }
        return result;
    }
}
