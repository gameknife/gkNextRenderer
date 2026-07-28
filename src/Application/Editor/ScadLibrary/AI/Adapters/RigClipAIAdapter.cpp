#include "Engine/Common/CoreMinimal.hpp"
#include "RigClipAIAdapter.hpp"

#include "../ScadAIValidationPolicy.hpp"

#include <cmath>
#include <unordered_set>

namespace ScadLibrary::AI
{
    namespace
    {
        constexpr std::string_view kRigSchema = R"json(
{
  "type":"object",
  "additionalProperties":false,
  "required":["version","summary","operations"],
  "properties":{
    "version":{"type":"integer","const":1},
    "summary":{"type":"string"},
    "operations":{"type":"array","maxItems":128,"items":{
      "type":"object",
      "required":["type"],
      "properties":{
        "type":{"enum":["create_clip","replace_clip","set_clip_meta","upsert_channel",
                        "remove_channel","upsert_key","remove_key"]},
        "id":{"type":"string"},"clip":{"type":"object"},"changes":{"type":"object"},
        "bone":{"type":"string"},"channel":{"enum":["pos","rot","scale"]},
        "time":{"type":"number"},"value":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3}
      }
    }}
  }
})json";

        bool IsName(const std::string& name)
        {
            return !name.empty() && name.size() <= 64 &&
                std::all_of(name.begin(), name.end(), [](const char character)
                { return std::isalnum(static_cast<unsigned char>(character)) || character == '_'; });
        }

        bool IsVector3(const nlohmann::json& value)
        {
            return value.is_array() && value.size() == 3 &&
                std::all_of(value.begin(), value.end(), [](const auto& item)
                { return item.is_number() && std::isfinite(item.template get<double>()); });
        }

        nlohmann::json* FindClip(nlohmann::json& clips, const std::string& id)
        {
            const auto found = std::find_if(clips.begin(), clips.end(), [&](const auto& clip)
            { return clip.is_object() && clip.value("id", clip.value("name", "")) == id; });
            return found == clips.end() ? nullptr : &*found;
        }

        nlohmann::json* FindChannel(nlohmann::json& clip, const std::string& bone, const std::string& channel)
        {
            if (!clip.contains("channels") || !clip["channels"].is_array())
            {
                return nullptr;
            }
            const auto found = std::find_if(clip["channels"].begin(), clip["channels"].end(), [&](const auto& item)
            { return item.value("bone", "") == bone && item.value("channel", "") == channel; });
            return found == clip["channels"].end() ? nullptr : &*found;
        }

        bool ValidateClip(nlohmann::json& clip, const std::unordered_set<std::string>& bones, std::string& error)
        {
            if (!RequireExactObjectKeys(clip, {"name", "loop", "channels"}, {"id", "duration"}, error) ||
                !clip["name"].is_string() || !IsName(clip["name"].get<std::string>()) ||
                !clip["loop"].is_boolean() || !clip["channels"].is_array())
            {
                error = error.empty() ? "clip fields are invalid" : error;
                return false;
            }
            if (clip["channels"].size() > FScadAIValidationPolicy::maxRigChannels)
            {
                error = "clip channel limit exceeded";
                return false;
            }
            std::unordered_set<std::string> channelKeys;
            double duration = 0.0;
            for (auto& channel : clip["channels"])
            {
                if (!RequireExactObjectKeys(channel, {"bone", "channel", "keys"}, {}, error) ||
                    !channel["bone"].is_string() || !channel["channel"].is_string() ||
                    !channel["keys"].is_array())
                {
                    return false;
                }
                const std::string bone = channel["bone"].get<std::string>();
                const std::string type = channel["channel"].get<std::string>();
                if (!bones.contains(bone) || (type != "pos" && type != "rot" && type != "scale") ||
                    !channelKeys.insert(bone + "#" + type).second)
                {
                    error = "unknown bone/channel or duplicate channel";
                    return false;
                }
                if (channel["keys"].empty() ||
                    channel["keys"].size() > FScadAIValidationPolicy::maxRigKeysPerChannel)
                {
                    error = "channel key count is invalid";
                    return false;
                }
                std::sort(channel["keys"].begin(), channel["keys"].end(), [](const auto& lhs, const auto& rhs)
                { return lhs.value("time", 0.0) < rhs.value("time", 0.0); });
                double previous = -1.0;
                for (const auto& key : channel["keys"])
                {
                    double time = 0.0;
                    if (!RequireExactObjectKeys(key, {"time", "value"}, {}, error) ||
                        !ReadFiniteNumber(key, "time", time, error) || time < 0.0 ||
                        time <= previous || !IsVector3(key["value"]))
                    {
                        error = error.empty() ? "rig keys require increasing non-negative finite times" : error;
                        return false;
                    }
                    if (type == "scale" &&
                        std::any_of(key["value"].begin(), key["value"].end(),
                                    [](const auto& item) { return item.template get<double>() <= 0.0; }))
                    {
                        error = "scale keys must be positive";
                        return false;
                    }
                    previous = time;
                    duration = std::max(duration, time);
                }
            }
            clip["duration"] = duration;
            clip["id"] = clip["name"];
            return true;
        }
    } // namespace

    FScadAIRequestEnvelope FRigClipAIAdapter::BuildRequest(const FScadAIEditTarget& target,
                                                           const FScadDocumentRevision& revision,
                                                           const nlohmann::json& snapshot,
                                                           std::string instruction)
    {
        FScadAIRequestEnvelope request;
        request.target = target;
        request.baseRevision = revision;
        request.conversationKey = target.documentKey;
        request.instruction = std::move(instruction);
        request.systemPrompt =
            "You edit rigid-body character animation clips through typed operations. Return JSON only. "
            "Authoring space is OpenSCAD Z-up, metres for position, XYZ degrees for rotation, positive scale, "
            "and the character faces -Y. Bone names must come exactly from snapshot.bones. Channels are "
            "pos/rot/scale. Key times are finite, non-negative and strictly increasing. Do not emit anim_* SCAD, "
            "geometry, markdown, filesystem actions, or unused/null operation fields.";
        request.snapshot = snapshot;
        request.schemaName = "scad_rig_clip_operations_v1";
        request.jsonSchema = kRigSchema;
        request.strictSchema = false;
        return request;
    }

    FScadAIValidationResult FRigClipAIAdapter::Validate(const nlohmann::json& snapshot,
                                                        std::string_view response)
    {
        FScadAIValidationResult result;
        try
        {
            result.artifact = nlohmann::json::parse(response);
        }
        catch (const std::exception& exception)
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "json", exception.what()});
            return result;
        }
        std::string error;
        if (!RequireExactObjectKeys(result.artifact, {"version", "summary", "operations"}, {}, error) ||
            !result.artifact["version"].is_number_integer() || result.artifact["version"].get<int>() != 1 ||
            !result.artifact["summary"].is_string() || !result.artifact["operations"].is_array() ||
            result.artifact["operations"].empty() ||
            result.artifact["operations"].size() > FScadAIValidationPolicy::maxOperations ||
            !snapshot.contains("bones") || !snapshot["bones"].is_array() ||
            !snapshot.contains("clips") || !snapshot["clips"].is_array())
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "schema",
                                     error.empty() ? "invalid rig artifact or snapshot" : error});
            return result;
        }
        std::unordered_set<std::string> bones;
        for (const auto& bone : snapshot["bones"])
        {
            if (bone.is_string())
            {
                bones.insert(bone.get<std::string>());
            }
        }
        nlohmann::json clips = snapshot["clips"];
        for (const auto& operation : result.artifact["operations"])
        {
            if (!operation.is_object() || !operation.contains("type") || !operation["type"].is_string())
            {
                error = "each rig operation needs a type";
                break;
            }
            const std::string type = operation["type"].get<std::string>();
            if (type == "create_clip")
            {
                if (!RequireExactObjectKeys(operation, {"type", "clip"}, {}, error) ||
                    !operation["clip"].is_object())
                {
                    break;
                }
                nlohmann::json clip = operation["clip"];
                if (!ValidateClip(clip, bones, error) || FindClip(clips, clip["name"].get<std::string>()))
                {
                    if (error.empty()) error = "clip name already exists";
                    break;
                }
                clips.push_back(std::move(clip));
                result.semanticDiff.push_back("新增动作 " + clips.back()["name"].get<std::string>());
                continue;
            }
            if (!operation.contains("id") || !operation["id"].is_string())
            {
                error = type + " requires clip id";
                break;
            }
            const std::string id = operation["id"].get<std::string>();
            nlohmann::json* clip = FindClip(clips, id);
            if (!clip)
            {
                error = "unknown clip id " + id;
                break;
            }
            if (type == "replace_clip")
            {
                if (!RequireExactObjectKeys(operation, {"type", "id", "clip"}, {}, error) ||
                    !operation["clip"].is_object())
                {
                    break;
                }
                nlohmann::json candidate = operation["clip"];
                if (!ValidateClip(candidate, bones, error))
                {
                    break;
                }
                const std::string name = candidate["name"].get<std::string>();
                const auto duplicate = std::find_if(clips.begin(), clips.end(), [&](const auto& item)
                { return &item != clip && item.value("name", "") == name; });
                if (duplicate != clips.end())
                {
                    error = "replacement clip name already exists";
                    break;
                }
                *clip = std::move(candidate);
                result.semanticDiff.push_back("替换动作 " + id);
            }
            else if (type == "set_clip_meta")
            {
                if (!RequireExactObjectKeys(operation, {"type", "id", "changes"}, {}, error) ||
                    !operation["changes"].is_object() || operation["changes"].empty())
                {
                    break;
                }
                for (const auto& [key, value] : operation["changes"].items())
                {
                    if (key != "name" && key != "loop")
                    {
                        error = "clip meta only allows name/loop";
                        break;
                    }
                    (*clip)[key] = value;
                }
                if (!error.empty() || !ValidateClip(*clip, bones, error))
                {
                    break;
                }
                result.semanticDiff.push_back("修改动作属性 " + id);
            }
            else if (type == "upsert_channel")
            {
                if (!RequireExactObjectKeys(operation, {"type", "id", "clip"}, {}, error) ||
                    !operation["clip"].is_object())
                {
                    break;
                }
                nlohmann::json channel = operation["clip"];
                if (!channel.contains("bone") || !channel["bone"].is_string() ||
                    !channel.contains("channel") || !channel["channel"].is_string())
                {
                    error = "upsert_channel clip field must contain one channel object";
                    break;
                }
                nlohmann::json* existing =
                    FindChannel(*clip, channel["bone"].get<std::string>(), channel["channel"].get<std::string>());
                if (existing) *existing = std::move(channel); else (*clip)["channels"].push_back(std::move(channel));
                if (!ValidateClip(*clip, bones, error))
                {
                    break;
                }
                result.semanticDiff.push_back("写入动作通道 " + id);
            }
            else if (type == "remove_channel")
            {
                if (!RequireExactObjectKeys(operation, {"type", "id", "bone", "channel"}, {}, error) ||
                    !operation["bone"].is_string() || !operation["channel"].is_string())
                {
                    break;
                }
                const std::string bone = operation["bone"].get<std::string>();
                const std::string channel = operation["channel"].get<std::string>();
                auto& channels = (*clip)["channels"];
                const auto found = std::find_if(channels.begin(), channels.end(), [&](const auto& item)
                { return item.value("bone", "") == bone && item.value("channel", "") == channel; });
                if (found == channels.end())
                {
                    error = "channel does not exist";
                    break;
                }
                channels.erase(found);
                if (!ValidateClip(*clip, bones, error))
                {
                    break;
                }
                result.semanticDiff.push_back("删除动作通道 " + id);
            }
            else if (type == "upsert_key" || type == "remove_key")
            {
                const bool upsert = type == "upsert_key";
                if (!RequireExactObjectKeys(operation, {"type", "id", "bone", "channel", "time"},
                                            upsert ? std::initializer_list<std::string_view>{"value"}
                                                   : std::initializer_list<std::string_view>{},
                                            error) ||
                    !operation["bone"].is_string() || !operation["channel"].is_string())
                {
                    break;
                }
                double time = 0.0;
                if (!ReadFiniteNumber(operation, "time", time, error) || time < 0.0 ||
                    (upsert && (!operation.contains("value") || !IsVector3(operation["value"]))))
                {
                    break;
                }
                nlohmann::json* channel = FindChannel(
                    *clip, operation["bone"].get<std::string>(), operation["channel"].get<std::string>());
                if (!channel)
                {
                    if (!upsert)
                    {
                        error = "channel does not exist";
                        break;
                    }
                    (*clip)["channels"].push_back({{"bone", operation["bone"]},
                                                   {"channel", operation["channel"]},
                                                   {"keys", nlohmann::json::array()}});
                    channel = &(*clip)["channels"].back();
                }
                auto& keys = (*channel)["keys"];
                const auto key = std::find_if(keys.begin(), keys.end(), [&](const auto& item)
                { return std::abs(item.value("time", -1.0) - time) < 1.0e-5; });
                if (upsert)
                {
                    const nlohmann::json value = {{"time", time}, {"value", operation["value"]}};
                    if (key == keys.end()) keys.push_back(value); else *key = value;
                }
                else
                {
                    if (key == keys.end())
                    {
                        error = "key does not exist";
                        break;
                    }
                    keys.erase(key);
                    if (keys.empty())
                    {
                        error = "remove_channel must be used to remove the final key";
                        break;
                    }
                }
                if (!ValidateClip(*clip, bones, error))
                {
                    break;
                }
                result.semanticDiff.push_back((upsert ? "写入关键帧 " : "删除关键帧 ") + id);
            }
            else
            {
                error = "unknown rig operation " + type;
                break;
            }
        }
        if (!error.empty())
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "domain", error});
            return result;
        }
        std::unordered_set<std::string> names;
        for (auto& clip : clips)
        {
            if (!ValidateClip(clip, bones, error) || !names.insert(clip["name"].get<std::string>()).second)
            {
                if (error.empty()) error = "duplicate clip name";
                result.issues.push_back({EScadAIValidationSeverity::Error, "clip", error});
                return result;
            }
        }
        result.success = true;
        result.summary = result.artifact["summary"].get<std::string>();
        result.candidate = {{"clips", std::move(clips)}};
        return result;
    }
} // namespace ScadLibrary::AI
