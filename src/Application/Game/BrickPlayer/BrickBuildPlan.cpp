#include "BrickBuildPlan.hpp"

#include <nlohmann/json.hpp>

namespace BrickPlayer
{
    namespace
    {
        glm::vec3 ParseVec3(const nlohmann::json& value)
        {
            if (!value.is_array() || value.size() != 3) throw std::invalid_argument("expected vec3 array");
            return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
        }
    }

    FBrickBuildPlan ParseBrickBuildPlan(const nlohmann::json& json)
    {
        FBrickBuildPlan plan;
        plan.version = json.at("version").get<int>();
        for (const auto& item : json.at("parts"))
        {
            plan.parts.push_back({item.at("id").get<std::string>(), item.at("part_id").get<std::string>(),
                                  item.at("color").get<int>(), ParseVec3(item.at("position")),
                                  ParseVec3(item.at("rotation_degrees"))});
        }
        if (json.contains("connections"))
        {
            for (const auto& item : json.at("connections"))
            {
                plan.connections.push_back({item.at("part_a").get<std::string>(),
                                            item.at("connector_a").get<std::string>(),
                                            item.at("part_b").get<std::string>(),
                                            item.at("connector_b").get<std::string>()});
            }
        }
        return plan;
    }

    FBrickBuildValidation ValidateBrickBuildPlan(const FBrickBuildPlan& plan,
                                                 const FBrickBuildConstraints& constraints)
    {
        FBrickBuildValidation result;
        if (plan.version != 1) result.errors.push_back("unsupported plan version");
        if (plan.parts.empty()) result.errors.push_back("plan contains no parts");
        std::unordered_set<std::string> ids;
        std::unordered_map<std::string, int> used;
        for (const auto& part : plan.parts)
        {
            if (part.id.empty() || !ids.insert(part.id).second) result.errors.push_back("duplicate or empty part id: " + part.id);
            if (!constraints.availablePartIds.empty() && !constraints.availablePartIds.contains(part.partId)) result.errors.push_back("unknown part: " + part.partId);
            if (++used[part.partId] > (constraints.inventory.contains(part.partId) ? constraints.inventory.at(part.partId) : std::numeric_limits<int>::max())) result.errors.push_back("inventory exceeded: " + part.partId);
        }
        if (constraints.overlaps)
        {
            for (size_t i = 0; i < plan.parts.size(); ++i) for (size_t j = i + 1; j < plan.parts.size(); ++j)
                if (constraints.overlaps(plan.parts[i], plan.parts[j])) result.errors.push_back("collision: " + plan.parts[i].id + "/" + plan.parts[j].id);
        }
        for (const auto& connection : plan.connections)
        {
            if (!ids.contains(connection.partA) || !ids.contains(connection.partB)) result.errors.push_back("connection references an unknown part");
            else if (constraints.connectionExists && !constraints.connectionExists(connection)) result.errors.push_back("illegal connection: " + connection.partA + "/" + connection.partB);
        }
        result.valid = result.errors.empty();
        return result;
    }

    std::string_view BrickBuildPlanJsonSchema()
    {
        return R"json({"type":"object","additionalProperties":false,"required":["version","parts","connections"],"properties":{"version":{"const":1},"parts":{"type":"array","minItems":1,"items":{"type":"object","additionalProperties":false,"required":["id","part_id","color","position","rotation_degrees"],"properties":{"id":{"type":"string"},"part_id":{"type":"string"},"color":{"type":"integer"},"position":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}},"rotation_degrees":{"type":"array","minItems":3,"maxItems":3,"items":{"type":"number"}}}}},"connections":{"type":"array","items":{"type":"object","additionalProperties":false,"required":["part_a","connector_a","part_b","connector_b"],"properties":{"part_a":{"type":"string"},"connector_a":{"type":"string"},"part_b":{"type":"string"},"connector_b":{"type":"string"}}}}}})json";
    }
}
