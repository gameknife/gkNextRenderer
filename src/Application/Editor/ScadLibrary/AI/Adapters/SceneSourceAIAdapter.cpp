#include "Engine/Common/CoreMinimal.hpp"
#include "SceneSourceAIAdapter.hpp"

#include "../ScadAIValidationPolicy.hpp"
#include "Modules/ScadLoader/FScadSourceIndex.h"

namespace ScadLibrary::AI
{
    namespace
    {
        constexpr std::string_view kSceneSourceSchema = R"json(
{
  "type":"object",
  "additionalProperties":false,
  "required":["version","summary","source"],
  "properties":{
    "version":{"type":"integer","const":1},
    "summary":{"type":"string"},
    "source":{"type":"string"}
  }
})json";
    }

    FScadAIRequestEnvelope FSceneSourceAIAdapter::BuildRequest(const FScadAIEditTarget& target,
                                                               const FScadDocumentRevision& revision,
                                                               const std::string& source,
                                                               std::string instruction)
    {
        FScadAIRequestEnvelope request;
        request.target = target;
        request.baseRevision = revision;
        request.conversationKey = target.documentKey;
        request.instruction = std::move(instruction);
        request.systemPrompt =
            "You edit one OpenSCAD scene. Return only the JSON artifact required by the schema. "
            "Keep existing use/include dependencies and unrelated authored code. Never return markdown, "
            "filesystem paths to modify, shell commands, or unified diff. The source must be a complete "
            "candidate for the selected scene file. Use OpenSCAD's right-handed axes: +X red, +Y green, "
            "+Z blue/up; positive rotations follow the right-hand rule.";
        request.snapshot = {{"source", source}};
        request.schemaName = "scad_scene_source_v1";
        request.jsonSchema = kSceneSourceSchema;
        return request;
    }

    FScadAIValidationResult FSceneSourceAIAdapter::Validate(const std::string& baseSource,
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
        if (!RequireExactObjectKeys(result.artifact, {"version", "summary", "source"}, {}, error))
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "schema", error});
            return result;
        }
        if (!result.artifact["version"].is_number_integer() || result.artifact["version"].get<int>() != 1 ||
            !result.artifact["summary"].is_string() || !result.artifact["source"].is_string())
        {
            result.issues.push_back(
                {EScadAIValidationSeverity::Error, "schema", "version/summary/source 类型不符合协议"});
            return result;
        }
        const std::string candidate = result.artifact["source"].get<std::string>();
        if (candidate.empty() || candidate.size() > FScadAIValidationPolicy::maxSourceBytes)
        {
            result.issues.push_back(
                {EScadAIValidationSeverity::Error, "source_size", "候选源码为空或超过大小限制"});
            return result;
        }
        Assets::Scad::FScadSourceIndex index;
        if (!Assets::Scad::BuildScadSourceIndex(candidate, index, error))
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "parse", error});
            return result;
        }
        result.success = true;
        result.summary = result.artifact["summary"].get<std::string>();
        result.candidate = {{"source", candidate}};
        result.semanticDiff.push_back(
            fmt::format("源码 {} → {} bytes", baseSource.size(), candidate.size()));
        return result;
    }
} // namespace ScadLibrary::AI
