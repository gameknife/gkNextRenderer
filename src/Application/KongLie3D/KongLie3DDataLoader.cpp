#include "KongLie3DDataLoader.hpp"

#include "Utilities/Exception.hpp"
#include "Utilities/FileHelper.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace
{
    [[noreturn]] void LogAndThrow(const std::string& message)
    {
        SPDLOG_ERROR("[KongLie3D] {}", message);
        Throw(std::runtime_error(message));
    }

    json LoadJsonFile(const std::string& relativePath)
    {
        const std::string absolutePath = Utilities::FileHelper::GetPlatformFilePath(relativePath.c_str());
        std::ifstream input(absolutePath);
        if (!input.is_open())
        {
            LogAndThrow(fmt::format("Failed to open JSON file: {}", absolutePath));
        }

        try
        {
            json document;
            input >> document;
            return document;
        }
        catch (const std::exception& exception)
        {
            LogAndThrow(fmt::format("Failed to parse JSON file {}: {}", absolutePath, exception.what()));
        }
    }

    template <typename TValue>
    TValue GetRequiredValue(const json& object, const char* key, const std::string& context)
    {
        if (!object.is_object())
        {
            LogAndThrow(fmt::format("{} is not a JSON object", context));
        }

        if (!object.contains(key))
        {
            LogAndThrow(fmt::format("{} is missing required field '{}'", context, key));
        }

        try
        {
            return object.at(key).get<TValue>();
        }
        catch (const std::exception& exception)
        {
            LogAndThrow(fmt::format("{} field '{}' has invalid type: {}", context, key, exception.what()));
        }
    }

    glm::vec3 GetRequiredColor(const json& object, const std::string& context)
    {
        if (!object.contains("color"))
        {
            LogAndThrow(fmt::format("{} is missing required field 'color'", context));
        }

        const json& colorJson = object.at("color");
        if (!colorJson.is_array() || colorJson.size() != 3)
        {
            LogAndThrow(fmt::format("{} field 'color' must be an array of 3 floats", context));
        }

        try
        {
            return glm::vec3(colorJson.at(0).get<float>(), colorJson.at(1).get<float>(), colorJson.at(2).get<float>());
        }
        catch (const std::exception& exception)
        {
            LogAndThrow(fmt::format("{} field 'color' has invalid value: {}", context, exception.what()));
        }
    }

    KongLie3D::FPieceDef ParsePieceDef(const std::string& pieceId, const json& pieceJson)
    {
        const std::string context = fmt::format("piece '{}'", pieceId);
        KongLie3D::FPieceDef def{};
        def.name = GetRequiredValue<std::string>(pieceJson, "name", context);
        def.team = GetRequiredValue<std::string>(pieceJson, "team", context);
        def.role = GetRequiredValue<std::string>(pieceJson, "role", context);
        def.attackType = GetRequiredValue<std::string>(pieceJson, "attackType", context);
        def.hp = GetRequiredValue<int>(pieceJson, "hp", context);
        def.atk = GetRequiredValue<int>(pieceJson, "atk", context);
        def.atkSpeed = GetRequiredValue<float>(pieceJson, "atkSpeed", context);
        def.range = GetRequiredValue<int>(pieceJson, "range", context);
        def.moveSpeed = GetRequiredValue<float>(pieceJson, "moveSpeed", context);
        def.maxMana = GetRequiredValue<int>(pieceJson, "maxMana", context);
        def.manaPerAtk = GetRequiredValue<int>(pieceJson, "manaPerAtk", context);
        if (def.role == "support")
        {
            def.healAmount = GetRequiredValue<int>(pieceJson, "healAmount", context);
            def.healInterval = GetRequiredValue<int>(pieceJson, "healInterval", context);
        }
        def.color = GetRequiredColor(pieceJson, context);
        def.isHero = GetRequiredValue<bool>(pieceJson, "isHero", context);

        if (!pieceJson.contains("skills") || !pieceJson.at("skills").is_object())
        {
            LogAndThrow(fmt::format("{} is missing required object 'skills'", context));
        }

        const json& skillsJson = pieceJson.at("skills");
        if (!skillsJson.contains("w") || !skillsJson.at("w").is_object())
        {
            LogAndThrow(fmt::format("{} is missing required object 'skills.w'", context));
        }
        if (!skillsJson.contains("ultimate") || !skillsJson.at("ultimate").is_object())
        {
            LogAndThrow(fmt::format("{} is missing required object 'skills.ultimate'", context));
        }

        def.skillWName = GetRequiredValue<std::string>(skillsJson.at("w"), "name", fmt::format("{} skills.w", context));
        def.skillW = GetRequiredValue<std::string>(skillsJson.at("w"), "effect", fmt::format("{} skills.w", context));
        def.skillWCooldownMs = GetRequiredValue<int>(skillsJson.at("w"), "cooldown", fmt::format("{} skills.w", context));
        def.skillUltimateName =
            GetRequiredValue<std::string>(skillsJson.at("ultimate"), "name", fmt::format("{} skills.ultimate", context));
        def.skillUltimate =
            GetRequiredValue<std::string>(skillsJson.at("ultimate"), "effect", fmt::format("{} skills.ultimate", context));
        return def;
    }

    KongLie3D::FPlacementEntry ParsePlacementEntry(const json& entryJson, const std::string& context)
    {
        KongLie3D::FPlacementEntry entry{};
        entry.pieceId = GetRequiredValue<std::string>(entryJson, "pieceId", context);
        entry.col = GetRequiredValue<int>(entryJson, "col", context);
        entry.row = GetRequiredValue<int>(entryJson, "row", context);
        return entry;
    }

    KongLie3D::FRelicDef ParseRelicDef(const json& relicJson, size_t index)
    {
        const std::string context = fmt::format("relics[{}]", index);
        KongLie3D::FRelicDef relic{};
        relic.id = GetRequiredValue<std::string>(relicJson, "id", context);
        relic.name = GetRequiredValue<std::string>(relicJson, "name", context);
        relic.icon = GetRequiredValue<std::string>(relicJson, "icon", context);
        relic.buffs = GetRequiredValue<std::vector<std::string>>(relicJson, "buffs", context);
        relic.statKey = GetRequiredValue<std::string>(relicJson, "statKey", context);
        relic.statVal = GetRequiredValue<float>(relicJson, "statVal", context);
        relic.color = GetRequiredColor(relicJson, context);
        return relic;
    }
}

namespace KongLie3D
{
    std::map<std::string, FPieceDef> LoadPieces(const std::string& path)
    {
        const json document = LoadJsonFile(path);
        if (!document.contains("pieces") || !document.at("pieces").is_object())
        {
            LogAndThrow(fmt::format("pieces file '{}' is missing required object 'pieces'", path));
        }

        std::map<std::string, FPieceDef> pieces;
        for (const auto& [pieceId, pieceJson] : document.at("pieces").items())
        {
            pieces.emplace(pieceId, ParsePieceDef(pieceId, pieceJson));
        }
        return pieces;
    }

    FPlacementData LoadPlacement(const std::string& path)
    {
        const json document = LoadJsonFile(path);
        FPlacementData placement{};

        if (!document.contains("player") || !document.at("player").is_array())
        {
            LogAndThrow(fmt::format("placement file '{}' is missing required array 'player'", path));
        }
        if (!document.contains("enemy") || !document.at("enemy").is_array())
        {
            LogAndThrow(fmt::format("placement file '{}' is missing required array 'enemy'", path));
        }
        if (!document.contains("bench") || !document.at("bench").is_array())
        {
            LogAndThrow(fmt::format("placement file '{}' is missing required array 'bench'", path));
        }

        for (size_t index = 0; index < document.at("player").size(); ++index)
        {
            placement.player.push_back(
                ParsePlacementEntry(document.at("player").at(index), fmt::format("placement.player[{}]", index)));
        }

        for (size_t index = 0; index < document.at("enemy").size(); ++index)
        {
            placement.enemy.push_back(
                ParsePlacementEntry(document.at("enemy").at(index), fmt::format("placement.enemy[{}]", index)));
        }

        for (size_t index = 0; index < document.at("bench").size(); ++index)
        {
            const json& benchEntry = document.at("bench").at(index);
            if (!benchEntry.is_string())
            {
                LogAndThrow(fmt::format("placement.bench[{}] must be a string piece id", index));
            }
            placement.bench.push_back(benchEntry.get<std::string>());
        }

        return placement;
    }

    std::vector<FRelicDef> LoadRelics(const std::string& path)
    {
        const json document = LoadJsonFile(path);
        if (!document.contains("relics") || !document.at("relics").is_array())
        {
            LogAndThrow(fmt::format("relics file '{}' is missing required array 'relics'", path));
        }

        std::vector<FRelicDef> relics;
        relics.reserve(document.at("relics").size());
        for (size_t index = 0; index < document.at("relics").size(); ++index)
        {
            relics.push_back(ParseRelicDef(document.at("relics").at(index), index));
        }
        return relics;
    }
}
