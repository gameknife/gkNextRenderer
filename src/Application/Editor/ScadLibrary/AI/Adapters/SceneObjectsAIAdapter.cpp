#include "Engine/Common/CoreMinimal.hpp"
#include "SceneObjectsAIAdapter.hpp"

#include "../ScadAIValidationPolicy.hpp"
#include "Modules/ScadLoader/FScadLexer.h"
#include "Modules/ScadLoader/FScadParser.h"

#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace ScadLibrary::AI
{
    namespace
    {
        constexpr std::string_view kSceneObjectsSchema = R"json(
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
        "type":{"enum":["add","update","remove","duplicate","reorder"]},
        "id":{"type":"string"},"newId":{"type":"string"},"beforeId":{"type":["string","null"]},
        "object":{"type":"object"},"changes":{"type":"object"}
      }
    }}
  }
})json";

        bool IsFiniteVector(const nlohmann::json& value, size_t count)
        {
            if (!value.is_array() || value.size() != count)
            {
                return false;
            }
            return std::all_of(value.begin(), value.end(), [](const nlohmann::json& item)
            { return item.is_number() && std::isfinite(item.get<double>()); });
        }

        bool ValidateObject(const nlohmann::json& object,
                            const std::unordered_set<std::string>& catalog, std::string& error)
        {
            if (!RequireExactObjectKeys(object, {"kitIndex", "module", "position", "rotation", "scale"},
                                        {"arguments", "color", "id"}, error))
            {
                return false;
            }
            if (!object["kitIndex"].is_number_integer() || !object["module"].is_string() ||
                !IsFiniteVector(object["position"], 3) || !IsFiniteVector(object["rotation"], 3) ||
                !IsFiniteVector(object["scale"], 3))
            {
                error = "scene object has invalid field types";
                return false;
            }
            const std::string catalogKey =
                fmt::format("{}#{}", object["kitIndex"].get<int>(), object["module"].get<std::string>());
            if (!catalog.contains(catalogKey))
            {
                error = "scene object references a module outside the current catalog";
                return false;
            }
            if (object.contains("arguments") &&
                (!object["arguments"].is_string() || object["arguments"].get<std::string>().size() >= 512))
            {
                error = "scene object arguments are invalid or too long";
                return false;
            }
            if (object.contains("arguments"))
            {
                const std::string module = object["module"].get<std::string>();
                const std::string wrapper = fmt::format(
                    "module __scadlibrary_validate_args__() {{ {}({}); }}",
                    module, object["arguments"].get<std::string>());
                std::vector<Assets::Scad::Token> tokens;
                Assets::Scad::Scope scope;
                if (!Assets::Scad::ScadLexer::Tokenize(wrapper, tokens, error) ||
                    !Assets::Scad::ScadParser::Parse(tokens, scope, error) || scope.size() != 1 ||
                    scope.front()->kind != Assets::Scad::StmtKind::ModuleDef ||
                    scope.front()->body.size() != 1 ||
                    scope.front()->body.front()->kind != Assets::Scad::StmtKind::Instance ||
                    scope.front()->body.front()->name != module)
                {
                    if (error.empty()) error = "arguments must remain one module call argument list";
                    return false;
                }
            }
            if (object.contains("color") && !IsFiniteVector(object["color"], 4))
            {
                error = "scene object color must contain four finite values";
                return false;
            }
            for (const auto& scale : object["scale"])
            {
                if (std::abs(scale.get<double>()) < 1.0e-6)
                {
                    error = "scene object scale cannot be zero";
                    return false;
                }
            }
            return true;
        }

        bool ValidateChanges(const nlohmann::json& changes, std::string& error)
        {
            if (!changes.is_object())
            {
                error = "changes must be an object";
                return false;
            }
            static const std::unordered_set<std::string> allowed{
                "kitIndex", "module", "position", "rotation", "scale", "arguments", "color"};
            for (const auto& [key, value] : changes.items())
            {
                (void)value;
                if (!allowed.contains(key))
                {
                    error = fmt::format("unknown scene object change '{}'", key);
                    return false;
                }
            }
            return !changes.empty();
        }
    } // namespace

    FScadAIRequestEnvelope FSceneObjectsAIAdapter::BuildRequest(const FScadAIEditTarget& target,
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
            "You edit a structured OpenSCAD scene using typed operations only. Return schema JSON only. "
            "Object ids are snapshot-local and must be copied exactly. add uses a unique newId and a complete "
            "object. update uses id plus changes. remove uses id. duplicate uses id/newId and optional changes. "
            "reorder uses id and beforeId (null means end). Modules must come from snapshot.catalog. Do not emit "
            "unused or null fields on an operation. Do not emit SCAD source, markdown, filesystem operations, "
            "or indices in place of ids. Positions and XYZ-degree rotations use OpenSCAD's right-handed axes: "
            "+X red, +Y green, +Z blue/up; positive rotations follow the right-hand rule. "
            "snapshot.selectedObject is the Kit instance explicitly selected by the user. When it is non-null, "
            "snapshot.selectionScope will be selected_instance; make that instance the primary edit subject "
            "unless the instruction clearly requests a broader scene change.";
        request.snapshot = snapshot;
        request.schemaName = "scad_scene_objects_v1";
        request.jsonSchema = kSceneObjectsSchema;
        // OpenAI strict schemas require every object property to be required and
        // additionalProperties=false. Operation payloads intentionally contain
        // sparse, type-dependent changes, so deterministic local validation is
        // the authority after provider-side best-effort schema generation.
        request.strictSchema = false;
        return request;
    }

    FScadAIValidationResult FSceneObjectsAIAdapter::Validate(const nlohmann::json& snapshot,
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
            !result.artifact["summary"].is_string() || !result.artifact["operations"].is_array())
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "schema",
                                     error.empty() ? "scene operation artifact field types are invalid" : error});
            return result;
        }
        if (result.artifact["operations"].empty() ||
            result.artifact["operations"].size() > FScadAIValidationPolicy::maxOperations)
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "operation_count",
                                     "operations must contain 1..128 entries"});
            return result;
        }
        if (!snapshot.contains("objects") || !snapshot["objects"].is_array() ||
            !snapshot.contains("catalog") || !snapshot["catalog"].is_array())
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "snapshot", "invalid scene snapshot"});
            return result;
        }

        std::unordered_set<std::string> catalog;
        for (const auto& item : snapshot["catalog"])
        {
            if (item.is_object() && item.contains("kitIndex") && item["kitIndex"].is_number_integer() &&
                item.contains("module") && item["module"].is_string())
            {
                catalog.insert(fmt::format("{}#{}", item["kitIndex"].get<int>(),
                                           item["module"].get<std::string>()));
            }
        }
        nlohmann::json objects = snapshot["objects"];
        auto rebuildIndex = [&]()
        {
            std::unordered_map<std::string, size_t> ids;
            for (size_t index = 0; index < objects.size(); ++index)
            {
                if (objects[index].contains("id") && objects[index]["id"].is_string())
                {
                    ids.emplace(objects[index]["id"].get<std::string>(), index);
                }
            }
            return ids;
        };

        std::unordered_set<std::string> proposalIds;
        for (const auto& operation : result.artifact["operations"])
        {
            if (!operation.is_object() || !operation.contains("type") || !operation["type"].is_string())
            {
                result.issues.push_back({EScadAIValidationSeverity::Error, "operation",
                                         "each operation needs a string type"});
                return result;
            }
            const std::string type = operation["type"].get<std::string>();
            const auto ids = rebuildIndex();
            if (type == "add")
            {
                if (!RequireExactObjectKeys(operation, {"type", "newId", "object"}, {}, error) ||
                    !operation["newId"].is_string() || !operation["object"].is_object())
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "add", error});
                    return result;
                }
                const std::string newId = operation["newId"].get<std::string>();
                if (newId.empty() || ids.contains(newId) || !proposalIds.insert(newId).second)
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "new_id",
                                             "add/duplicate newId must be unique"});
                    return result;
                }
                nlohmann::json object = operation["object"];
                object["id"] = newId;
                if (!ValidateObject(object, catalog, error))
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "object", error});
                    return result;
                }
                objects.push_back(std::move(object));
                result.semanticDiff.push_back("新增对象 " + newId);
            }
            else if (type == "update")
            {
                if (!RequireExactObjectKeys(operation, {"type", "id", "changes"}, {}, error) ||
                    !operation["id"].is_string() || !ValidateChanges(operation["changes"], error))
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "update", error});
                    return result;
                }
                const std::string id = operation["id"].get<std::string>();
                const auto found = ids.find(id);
                if (found == ids.end())
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "id", "unknown object id " + id});
                    return result;
                }
                nlohmann::json candidate = objects[found->second];
                for (const auto& [key, value] : operation["changes"].items())
                {
                    candidate[key] = value;
                }
                if (!ValidateObject(candidate, catalog, error))
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "object", error});
                    return result;
                }
                objects[found->second] = std::move(candidate);
                result.semanticDiff.push_back("修改对象 " + id);
            }
            else if (type == "remove")
            {
                if (!RequireExactObjectKeys(operation, {"type", "id"}, {}, error) ||
                    !operation["id"].is_string())
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "remove", error});
                    return result;
                }
                const std::string id = operation["id"].get<std::string>();
                const auto found = ids.find(id);
                if (found == ids.end())
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "id", "unknown object id " + id});
                    return result;
                }
                objects.erase(objects.begin() + static_cast<nlohmann::json::difference_type>(found->second));
                result.semanticDiff.push_back("删除对象 " + id);
            }
            else if (type == "duplicate")
            {
                if (!RequireExactObjectKeys(operation, {"type", "id", "newId"}, {"changes"}, error) ||
                    !operation["id"].is_string() || !operation["newId"].is_string())
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "duplicate", error});
                    return result;
                }
                const std::string id = operation["id"].get<std::string>();
                const std::string newId = operation["newId"].get<std::string>();
                const auto found = ids.find(id);
                if (found == ids.end() || newId.empty() || ids.contains(newId) ||
                    !proposalIds.insert(newId).second)
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "duplicate_id",
                                             "duplicate id/newId is invalid"});
                    return result;
                }
                nlohmann::json candidate = objects[found->second];
                candidate["id"] = newId;
                if (operation.contains("changes"))
                {
                    if (!ValidateChanges(operation["changes"], error))
                    {
                        result.issues.push_back({EScadAIValidationSeverity::Error, "changes", error});
                        return result;
                    }
                    for (const auto& [key, value] : operation["changes"].items())
                    {
                        candidate[key] = value;
                    }
                }
                if (!ValidateObject(candidate, catalog, error))
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "object", error});
                    return result;
                }
                objects.push_back(std::move(candidate));
                result.semanticDiff.push_back(fmt::format("复制对象 {} → {}", id, newId));
            }
            else if (type == "reorder")
            {
                if (!RequireExactObjectKeys(operation, {"type", "id", "beforeId"}, {}, error) ||
                    !operation["id"].is_string() ||
                    !(operation["beforeId"].is_null() || operation["beforeId"].is_string()))
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "reorder", error});
                    return result;
                }
                const std::string id = operation["id"].get<std::string>();
                auto found = ids.find(id);
                if (found == ids.end())
                {
                    result.issues.push_back({EScadAIValidationSeverity::Error, "id", "unknown object id " + id});
                    return result;
                }
                nlohmann::json moving = objects[found->second];
                objects.erase(objects.begin() + static_cast<nlohmann::json::difference_type>(found->second));
                if (operation["beforeId"].is_null())
                {
                    objects.push_back(std::move(moving));
                }
                else
                {
                    const std::string beforeId = operation["beforeId"].get<std::string>();
                    const auto afterErase = rebuildIndex();
                    const auto before = afterErase.find(beforeId);
                    if (before == afterErase.end())
                    {
                        result.issues.push_back({EScadAIValidationSeverity::Error, "before_id",
                                                 "unknown beforeId " + beforeId});
                        return result;
                    }
                    objects.insert(objects.begin() + static_cast<nlohmann::json::difference_type>(before->second),
                                   std::move(moving));
                }
                result.semanticDiff.push_back("重排对象 " + id);
            }
            else
            {
                result.issues.push_back({EScadAIValidationSeverity::Error, "operation_type",
                                         "unknown operation type " + type});
                return result;
            }
        }

        result.success = true;
        result.summary = result.artifact["summary"].get<std::string>();
        result.candidate = {{"objects", std::move(objects)}};
        return result;
    }
} // namespace ScadLibrary::AI
