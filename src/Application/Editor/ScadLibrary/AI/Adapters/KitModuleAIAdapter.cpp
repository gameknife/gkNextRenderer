#include "Engine/Common/CoreMinimal.hpp"
#include "KitModuleAIAdapter.hpp"

#include "../ScadAIValidationPolicy.hpp"
#include "Modules/ScadLoader/FScadSourceIndex.h"

#include <cctype>

namespace ScadLibrary::AI
{
    namespace
    {
        constexpr std::string_view kKitModuleSchema = R"json(
{
  "type":"object",
  "additionalProperties":false,
  "required":["version","summary","moduleName","moduleSource"],
  "properties":{
    "version":{"type":"integer","const":1},
    "summary":{"type":"string"},
    "moduleName":{"type":"string"},
    "moduleSource":{"type":"string"}
  }
})json";

        std::string CompactSignature(std::string_view signature)
        {
            std::string compact;
            for (const char character : signature)
            {
                if (!std::isspace(static_cast<unsigned char>(character)))
                {
                    compact.push_back(character);
                }
            }
            return compact;
        }
    } // namespace

    FScadAIRequestEnvelope FKitModuleAIAdapter::BuildRequest(const FScadAIEditTarget& target,
                                                              const FScadDocumentRevision& revision,
                                                              const std::string& kitSource,
                                                              std::string instruction)
    {
        Assets::Scad::FScadSourceIndex index;
        std::string error;
        std::string moduleSource;
        std::string signature;
        if (Assets::Scad::BuildScadSourceIndex(kitSource, index, error))
        {
            if (const auto* span = index.Find(Assets::Scad::EScadDefinitionKind::Module, target.primaryId))
            {
                moduleSource = kitSource.substr(span->begin, span->end - span->begin);
                signature = kitSource.substr(span->signatureBegin, span->signatureEnd - span->signatureBegin);
            }
        }
        FScadAIRequestEnvelope request;
        request.target = target;
        request.baseRevision = revision;
        request.conversationKey = target.documentKey + "#" + target.primaryId;
        request.instruction = std::move(instruction);
        request.systemPrompt =
            "You edit exactly one existing public OpenSCAD module. Return only the schema JSON. "
            "Keep moduleName and its parameter signature exactly unchanged. Return a complete module definition, "
            "not a file, diff, markdown, helper definition, or shell command. Existing helper modules/functions "
            "are read-only. Author in OpenSCAD's right-handed coordinate system: +X red, +Y green, +Z blue/up; "
            "positive rotations follow the right-hand rule.";
        request.snapshot = {
            {"moduleName", target.primaryId},
            {"signature", signature},
            {"moduleSource", moduleSource},
            {"readOnlyKitSource", kitSource},
        };
        request.schemaName = "scad_kit_module_v1";
        request.jsonSchema = kKitModuleSchema;
        return request;
    }

    FScadAIValidationResult FKitModuleAIAdapter::Validate(const std::string& kitSource,
                                                          const std::string& moduleName,
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
        if (!RequireExactObjectKeys(result.artifact,
                                    {"version", "summary", "moduleName", "moduleSource"}, {}, error))
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "schema", error});
            return result;
        }
        if (!result.artifact["version"].is_number_integer() || result.artifact["version"].get<int>() != 1 ||
            !result.artifact["summary"].is_string() || !result.artifact["moduleName"].is_string() ||
            !result.artifact["moduleSource"].is_string())
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "schema", "Kit artifact 字段类型错误"});
            return result;
        }
        if (result.artifact["moduleName"].get<std::string>() != moduleName)
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "identity", "模型修改了 module 名称"});
            return result;
        }

        Assets::Scad::FScadSourceIndex baseIndex;
        if (!Assets::Scad::BuildScadSourceIndex(kitSource, baseIndex, error))
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "base_parse", error});
            return result;
        }
        const auto* baseSpan = baseIndex.Find(Assets::Scad::EScadDefinitionKind::Module, moduleName);
        if (!baseSpan)
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "target", "目标 module 不存在"});
            return result;
        }
        const std::string moduleSource = result.artifact["moduleSource"].get<std::string>();
        if (moduleSource.empty() || moduleSource.size() > FScadAIValidationPolicy::maxSourceBytes)
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "source_size", "module 源码大小无效"});
            return result;
        }
        Assets::Scad::FScadSourceIndex moduleIndex;
        if (!Assets::Scad::BuildScadSourceIndex(moduleSource, moduleIndex, error))
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "parse", error});
            return result;
        }
        if (moduleIndex.definitions.size() != 1 ||
            moduleIndex.definitions.front().kind != Assets::Scad::EScadDefinitionKind::Module ||
            moduleIndex.definitions.front().name != moduleName)
        {
            result.issues.push_back(
                {EScadAIValidationSeverity::Error, "scope", "候选必须且只能包含目标 module 定义"});
            return result;
        }
        const auto& candidateSpan = moduleIndex.definitions.front();
        const std::string baseSignature =
            kitSource.substr(baseSpan->signatureBegin, baseSpan->signatureEnd - baseSpan->signatureBegin);
        const std::string candidateSignature = moduleSource.substr(
            candidateSpan.signatureBegin, candidateSpan.signatureEnd - candidateSpan.signatureBegin);
        if (CompactSignature(baseSignature) != CompactSignature(candidateSignature))
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "signature", "module 参数签名不允许修改"});
            return result;
        }
        std::string candidateKit = kitSource;
        candidateKit.replace(baseSpan->begin, baseSpan->end - baseSpan->begin, moduleSource);
        Assets::Scad::FScadSourceIndex candidateIndex;
        if (!Assets::Scad::BuildScadSourceIndex(candidateKit, candidateIndex, error))
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "kit_parse", error});
            return result;
        }
        if (candidateIndex.definitions.size() != baseIndex.definitions.size())
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "definitions",
                                     "替换后顶层定义集合发生意外变化"});
            return result;
        }
        for (size_t index = 0; index < baseIndex.definitions.size(); ++index)
        {
            if (baseIndex.definitions[index].kind != candidateIndex.definitions[index].kind ||
                baseIndex.definitions[index].name != candidateIndex.definitions[index].name)
            {
                result.issues.push_back({EScadAIValidationSeverity::Error, "definitions",
                                         "替换后顶层定义名称或顺序发生变化"});
                return result;
            }
        }
        result.success = true;
        result.summary = result.artifact["summary"].get<std::string>();
        result.candidate = {{"kitSource", candidateKit}, {"moduleSource", moduleSource}};
        result.semanticDiff.push_back(fmt::format("{}: {} → {} bytes", moduleName,
                                                  baseSpan->end - baseSpan->begin, moduleSource.size()));
        result.issues.push_back({EScadAIValidationSeverity::Warning, "shared_asset",
                                 "Kit 是共享资产，保存后会影响所有引用该 module 的场景"});
        return result;
    }
} // namespace ScadLibrary::AI
