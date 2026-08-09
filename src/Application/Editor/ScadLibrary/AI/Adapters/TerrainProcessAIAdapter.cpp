#include "Engine/Common/CoreMinimal.hpp"
#include "TerrainProcessAIAdapter.hpp"

#include "../ScadAIValidationPolicy.hpp"

#include <cmath>
#include <unordered_map>

namespace ScadLibrary::AI
{
    namespace
    {
        constexpr std::string_view kTerrainSchema = R"json(
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
        "type":{"enum":["set_terrain","add_feature","update_feature","remove_feature","move_feature",
                        "add_rule","update_rule","remove_rule","move_rule"]},
        "id":{"type":"string"},"newId":{"type":"string"},"beforeId":{"type":["string","null"]},
        "changes":{"type":"object"},"feature":{"type":"object"},"rule":{"type":"object"}
      }
    }}
  }
})json";

        bool AllFinite(const nlohmann::json& value)
        {
            if (value.is_number_float())
            {
                return std::isfinite(value.get<double>());
            }
            if (value.is_array())
            {
                return std::all_of(value.begin(), value.end(), AllFinite);
            }
            if (value.is_object())
            {
                return std::all_of(value.begin(), value.end(), AllFinite);
            }
            return true;
        }

        bool ValidateTerrain(const nlohmann::json& terrain, std::string& error)
        {
            if (!terrain.is_object() || !AllFinite(terrain))
            {
                error = "terrain fields must be a finite JSON object";
                return false;
            }
            if (terrain.contains("cells"))
            {
                const auto& cells = terrain["cells"];
                if (!cells.is_array() || cells.size() != 2 ||
                    !std::all_of(cells.begin(), cells.end(), [](const auto& value)
                                 { return value.is_number_integer() && value.template get<int>() >= 4 &&
                                     value.template get<int>() <= 256; }))
                {
                    error = "terrain cells must be two integers in [4, 256]";
                    return false;
                }
            }
            if (terrain.contains("size"))
            {
                const auto& size = terrain["size"];
                if (!size.is_array() || size.size() != 2 ||
                    !std::all_of(size.begin(), size.end(), [](const auto& value)
                                 { return value.is_number() && value.template get<double>() > 0.0; }))
                {
                    error = "terrain size must contain two positive values";
                    return false;
                }
            }
            return true;
        }

        bool ValidateNode(const nlohmann::json& node, bool feature, std::string& error)
        {
            if (!node.is_object() || !node.contains("type") || !node["type"].is_string() || !AllFinite(node))
            {
                error = feature ? "feature must contain a valid type" : "rule must contain a valid type";
                return false;
            }
            if (node.contains("points"))
            {
                const auto& points = node["points"];
                if (!points.is_array() || points.size() < 2 ||
                    !std::all_of(points.begin(), points.end(), [](const auto& point)
                                 { return point.is_array() && point.size() == 2 &&
                                     point[0].is_number() && point[1].is_number(); }))
                {
                    error = "points must contain at least two finite XY pairs";
                    return false;
                }
            }
            for (const char* positive : {"radius", "width", "step", "probe"})
            {
                if (node.contains(positive) &&
                    (!node[positive].is_number() || node[positive].get<double>() <= 0.0))
                {
                    error = fmt::format("{} must be positive", positive);
                    return false;
                }
            }
            if (node.contains("count") &&
                (!node["count"].is_number_integer() || node["count"].get<int>() < 0 ||
                 node["count"].get<int>() > 10000))
            {
                error = "count must be an integer in [0, 10000]";
                return false;
            }
            if (node.contains("variants") &&
                (!node["variants"].is_number_integer() || node["variants"].get<int>() < 0 ||
                 node["variants"].get<int>() > 10000))
            {
                error = "variants must be an integer in [0, 10000]";
                return false;
            }
            if (node.contains("scaleRange"))
            {
                const auto& scaleRange = node["scaleRange"];
                if (!scaleRange.is_array() || scaleRange.size() != 2 ||
                    !std::all_of(scaleRange.begin(), scaleRange.end(), [](const auto& value)
                                 { return value.is_number() && value.template get<double>() > 0.0; }))
                {
                    error = "scaleRange must contain two positive values";
                    return false;
                }
            }
            if (node.contains("maxSlope") &&
                (!node["maxSlope"].is_number() || node["maxSlope"].get<double>() < 0.0 ||
                 node["maxSlope"].get<double>() > 90.0))
            {
                error = "maxSlope must be in [0, 90]";
                return false;
            }
            return true;
        }

        std::unordered_map<std::string, size_t> IndexNodes(const nlohmann::json& nodes)
        {
            std::unordered_map<std::string, size_t> result;
            for (size_t index = 0; index < nodes.size(); ++index)
            {
                if (nodes[index].contains("id") && nodes[index]["id"].is_string())
                {
                    result.emplace(nodes[index]["id"].get<std::string>(), index);
                }
            }
            return result;
        }

        bool ApplyNodeOperation(nlohmann::json& nodes, const nlohmann::json& operation, bool feature,
                                std::vector<std::string>& diff, std::string& error)
        {
            const std::string noun = feature ? "feature" : "rule";
            const std::string type = operation["type"].get<std::string>();
            const auto ids = IndexNodes(nodes);
            const std::string addType = "add_" + noun;
            const std::string updateType = "update_" + noun;
            const std::string removeType = "remove_" + noun;
            const std::string moveType = "move_" + noun;
            if (type == addType)
            {
                if (!RequireExactObjectKeys(operation, {"type", "newId", noun}, {}, error) ||
                    !operation["newId"].is_string() || !operation[noun].is_object())
                {
                    return false;
                }
                const std::string newId = operation["newId"].get<std::string>();
                if (newId.empty() || ids.contains(newId))
                {
                    error = "new node id must be unique";
                    return false;
                }
                nlohmann::json node = operation[noun];
                node["id"] = newId;
                if (!ValidateNode(node, feature, error))
                {
                    return false;
                }
                nodes.push_back(std::move(node));
                diff.push_back("新增 " + noun + " " + newId);
                return true;
            }
            if (!operation.contains("id") || !operation["id"].is_string())
            {
                error = type + " requires an id";
                return false;
            }
            const std::string id = operation["id"].get<std::string>();
            const auto found = ids.find(id);
            if (found == ids.end())
            {
                error = "unknown node id " + id;
                return false;
            }
            if (type == updateType)
            {
                if (!RequireExactObjectKeys(operation, {"type", "id", "changes"}, {}, error) ||
                    !operation["changes"].is_object() || operation["changes"].empty() ||
                    operation["changes"].contains("id"))
                {
                    return false;
                }
                nlohmann::json candidate = nodes[found->second];
                for (const auto& [key, value] : operation["changes"].items())
                {
                    candidate[key] = value;
                }
                if (!ValidateNode(candidate, feature, error))
                {
                    return false;
                }
                nodes[found->second] = std::move(candidate);
                diff.push_back("修改 " + noun + " " + id);
                return true;
            }
            if (type == removeType)
            {
                if (!RequireExactObjectKeys(operation, {"type", "id"}, {}, error))
                {
                    return false;
                }
                nodes.erase(nodes.begin() + static_cast<nlohmann::json::difference_type>(found->second));
                diff.push_back("删除 " + noun + " " + id);
                return true;
            }
            if (type == moveType)
            {
                if (!RequireExactObjectKeys(operation, {"type", "id", "beforeId"}, {}, error) ||
                    !(operation["beforeId"].is_null() || operation["beforeId"].is_string()))
                {
                    return false;
                }
                nlohmann::json moving = nodes[found->second];
                nodes.erase(nodes.begin() + static_cast<nlohmann::json::difference_type>(found->second));
                if (operation["beforeId"].is_null())
                {
                    nodes.push_back(std::move(moving));
                }
                else
                {
                    const std::string beforeId = operation["beforeId"].get<std::string>();
                    const auto afterErase = IndexNodes(nodes);
                    const auto before = afterErase.find(beforeId);
                    if (before == afterErase.end())
                    {
                        error = "unknown beforeId " + beforeId;
                        return false;
                    }
                    nodes.insert(nodes.begin() + static_cast<nlohmann::json::difference_type>(before->second),
                                 std::move(moving));
                }
                diff.push_back("移动 " + noun + " " + id);
                return true;
            }
            error = "operation does not match selected node collection";
            return false;
        }
    } // namespace

    FScadAIRequestEnvelope FTerrainProcessAIAdapter::BuildRequest(const FScadAIEditTarget& target,
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
            "You edit a procedural terrain document with typed operations only. Return JSON only. "
            "Use snapshot ids fN/rN, never vector indices. set_terrain changes literal terrain fields. "
            "add/update/remove/move feature or rule changes only the corresponding typed node. Preserve ids, "
            "unknown authored SCAD, and comments. Emit only fields used by the selected operation; never add "
            "unused or null fields. Coordinates use OpenSCAD's right-handed axes: +X red, +Y green, +Z blue/up; "
            "positive rotations follow the right-hand rule. Do not emit SCAD source or filesystem actions.";
        request.snapshot = snapshot;
        request.schemaName = "scad_terrain_operations_v1";
        request.jsonSchema = kTerrainSchema;
        request.strictSchema = false;
        return request;
    }

    FScadAIValidationResult FTerrainProcessAIAdapter::Validate(const nlohmann::json& snapshot,
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
                                     error.empty() ? "invalid terrain artifact fields" : error});
            return result;
        }
        if (result.artifact["operations"].empty() ||
            result.artifact["operations"].size() > FScadAIValidationPolicy::maxOperations ||
            !snapshot.contains("terrain") || !snapshot.contains("features") || !snapshot.contains("rules"))
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "snapshot",
                                     "invalid operation count or terrain snapshot"});
            return result;
        }
        nlohmann::json terrain = snapshot["terrain"];
        nlohmann::json features = snapshot["features"];
        nlohmann::json rules = snapshot["rules"];
        for (const auto& operation : result.artifact["operations"])
        {
            if (!operation.is_object() || !operation.contains("type") || !operation["type"].is_string())
            {
                error = "each operation requires a type";
                break;
            }
            const std::string type = operation["type"].get<std::string>();
            if (type == "set_terrain")
            {
                if (!RequireExactObjectKeys(operation, {"type", "changes"}, {}, error) ||
                    !operation["changes"].is_object() || operation["changes"].empty())
                {
                    break;
                }
                nlohmann::json candidate = terrain;
                for (const auto& [key, value] : operation["changes"].items())
                {
                    candidate[key] = value;
                }
                if (!ValidateTerrain(candidate, error))
                {
                    break;
                }
                terrain = std::move(candidate);
                result.semanticDiff.push_back("修改 Terrain 参数");
            }
            else if (type.ends_with("_feature"))
            {
                if (!ApplyNodeOperation(features, operation, true, result.semanticDiff, error))
                {
                    break;
                }
            }
            else if (type.ends_with("_rule"))
            {
                if (!ApplyNodeOperation(rules, operation, false, result.semanticDiff, error))
                {
                    break;
                }
            }
            else
            {
                error = "unknown terrain operation " + type;
                break;
            }
            if (features.size() > FScadAIValidationPolicy::maxTerrainFeatures ||
                rules.size() > FScadAIValidationPolicy::maxTerrainRules)
            {
                error = "terrain feature/rule limit exceeded";
                break;
            }
        }
        if (!error.empty())
        {
            result.issues.push_back({EScadAIValidationSeverity::Error, "domain", error});
            return result;
        }
        result.success = true;
        result.summary = result.artifact["summary"].get<std::string>();
        result.candidate = {{"terrain", std::move(terrain)}, {"features", std::move(features)},
                            {"rules", std::move(rules)}};
        return result;
    }
} // namespace ScadLibrary::AI
