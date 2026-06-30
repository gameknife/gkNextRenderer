#include "KongLie3DDataLoader.hpp"

#include "Engine/Runtime/Utilities/JsonHelpers.h"
#include "Engine/Utilities/Exception.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace
{
    [[noreturn]] void DataLoaderLogAndThrow(const std::string& message)
    {
        SPDLOG_ERROR("[KongLie3D] {}", message);
        Throw(std::runtime_error(message));
    }

    KongLie3D::FPieceDef ParsePieceDef(const std::string& pieceId, const json& pieceJson)
    {
        const std::string context = fmt::format("piece '{}'", pieceId);
        KongLie3D::FPieceDef def{};
        def.name = NextJson::GetRequired<std::string>(pieceJson, "name", context);
        def.team = NextJson::GetRequired<std::string>(pieceJson, "team", context);
        def.role = NextJson::GetRequired<std::string>(pieceJson, "role", context);
        def.synergies = NextJson::GetOptional<std::vector<std::string>>(pieceJson, "synergies", context, {});
        def.attackType = NextJson::GetRequired<std::string>(pieceJson, "attackType", context);
        def.hp = NextJson::GetRequired<int>(pieceJson, "hp", context);
        def.atk = NextJson::GetRequired<int>(pieceJson, "atk", context);
        def.atkSpeed = NextJson::GetRequired<float>(pieceJson, "atkSpeed", context);
        def.range = NextJson::GetRequired<int>(pieceJson, "range", context);
        def.moveSpeed = NextJson::GetRequired<float>(pieceJson, "moveSpeed", context);
        def.maxMana = NextJson::GetRequired<int>(pieceJson, "maxMana", context);
        def.manaPerAtk = NextJson::GetRequired<int>(pieceJson, "manaPerAtk", context);
        if (def.role == "support")
        {
            def.healAmount = NextJson::GetRequired<int>(pieceJson, "healAmount", context);
            def.healInterval = NextJson::GetRequired<int>(pieceJson, "healInterval", context);
        }
        def.color = NextJson::GetRequiredVec3(pieceJson, "color", context);
        def.isHero = NextJson::GetRequired<bool>(pieceJson, "isHero", context);

        if (!pieceJson.contains("skills") || !pieceJson.at("skills").is_object())
        {
            DataLoaderLogAndThrow(fmt::format("{} is missing required object 'skills'", context));
        }

        const json& skillsJson = pieceJson.at("skills");
        if (!skillsJson.contains("w") || !skillsJson.at("w").is_object())
        {
            DataLoaderLogAndThrow(fmt::format("{} is missing required object 'skills.w'", context));
        }
        if (!skillsJson.contains("ultimate") || !skillsJson.at("ultimate").is_object())
        {
            DataLoaderLogAndThrow(fmt::format("{} is missing required object 'skills.ultimate'", context));
        }

        def.skillWName = NextJson::GetRequired<std::string>(skillsJson.at("w"), "name", fmt::format("{} skills.w", context));
        def.skillW = NextJson::GetRequired<std::string>(skillsJson.at("w"), "effect", fmt::format("{} skills.w", context));
        def.skillWDesc = NextJson::GetOptional<std::string>(skillsJson.at("w"), "desc", fmt::format("{} skills.w", context), "");
        def.skillWCooldownMs = NextJson::GetRequired<int>(skillsJson.at("w"), "cooldown", fmt::format("{} skills.w", context));
        def.skillUltimateName =
            NextJson::GetRequired<std::string>(skillsJson.at("ultimate"), "name", fmt::format("{} skills.ultimate", context));
        def.skillUltimate =
            NextJson::GetRequired<std::string>(skillsJson.at("ultimate"), "effect", fmt::format("{} skills.ultimate", context));
        def.skillUltimateDesc =
            NextJson::GetOptional<std::string>(skillsJson.at("ultimate"), "desc", fmt::format("{} skills.ultimate", context), "");
        return def;
    }

    KongLie3D::FPlacementEntry ParsePlacementEntry(const json& entryJson, const std::string& context)
    {
        KongLie3D::FPlacementEntry entry{};
        entry.pieceId = NextJson::GetRequired<std::string>(entryJson, "pieceId", context);
        entry.col = NextJson::GetRequired<int>(entryJson, "col", context);
        entry.row = NextJson::GetRequired<int>(entryJson, "row", context);
        return entry;
    }

    KongLie3D::FSynergyTier ParseSynergyTier(const json& tierJson, const std::string& context)
    {
        KongLie3D::FSynergyTier tier{};
        tier.count = NextJson::GetRequired<int>(tierJson, "count", context);
        tier.atkBonus = NextJson::GetOptional<float>(tierJson, "atkBonus", context, 0.0f);
        tier.hpBonus = NextJson::GetOptional<float>(tierJson, "hpBonus", context, 0.0f);
        tier.spdBonus = NextJson::GetOptional<float>(tierJson, "spdBonus", context, 0.0f);
        tier.apBonus = NextJson::GetOptional<float>(tierJson, "apBonus", context, 0.0f);
        return tier;
    }

    KongLie3D::FSynergyDef ParseSynergyDef(const json& synergyJson, size_t index)
    {
        const std::string context = fmt::format("synergies[{}]", index);
        KongLie3D::FSynergyDef synergy{};
        synergy.id = NextJson::GetRequired<std::string>(synergyJson, "id", context);
        synergy.name = NextJson::GetRequired<std::string>(synergyJson, "name", context);
        if (!synergyJson.contains("tiers") || !synergyJson.at("tiers").is_array())
        {
            DataLoaderLogAndThrow(fmt::format("{} is missing required array 'tiers'", context));
        }

        for (size_t tierIndex = 0; tierIndex < synergyJson.at("tiers").size(); ++tierIndex)
        {
            synergy.tiers.push_back(
                ParseSynergyTier(synergyJson.at("tiers").at(tierIndex), fmt::format("{} tiers[{}]", context, tierIndex)));
        }
        return synergy;
    }

    KongLie3D::FLevelDef ParseLevelDef(const json& levelJson, size_t index)
    {
        const std::string context = fmt::format("levels[{}]", index);
        KongLie3D::FLevelDef level{};
        level.id = NextJson::GetRequired<std::string>(levelJson, "id", context);
        level.name = NextJson::GetRequired<std::string>(levelJson, "name", context);
        level.enemyDmgMult = NextJson::GetRequired<float>(levelJson, "enemyDmgMult", context);
        if (!levelJson.contains("enemy") || !levelJson.at("enemy").is_array())
        {
            DataLoaderLogAndThrow(fmt::format("{} is missing required array 'enemy'", context));
        }
        if (!levelJson.contains("bench") || !levelJson.at("bench").is_array())
        {
            DataLoaderLogAndThrow(fmt::format("{} is missing required array 'bench'", context));
        }

        for (size_t enemyIndex = 0; enemyIndex < levelJson.at("enemy").size(); ++enemyIndex)
        {
            level.enemy.push_back(ParsePlacementEntry(levelJson.at("enemy").at(enemyIndex),
                                                      fmt::format("{} enemy[{}]", context, enemyIndex)));
        }

        for (size_t benchIndex = 0; benchIndex < levelJson.at("bench").size(); ++benchIndex)
        {
            const json& benchEntry = levelJson.at("bench").at(benchIndex);
            if (!benchEntry.is_string())
            {
                DataLoaderLogAndThrow(fmt::format("{} bench[{}] must be a string piece id", context, benchIndex));
            }
            level.bench.push_back(benchEntry.get<std::string>());
        }

        return level;
    }

    KongLie3D::FRelicDef ParseRelicDef(const json& relicJson, size_t index)
    {
        const std::string context = fmt::format("relics[{}]", index);
        KongLie3D::FRelicDef relic{};
        relic.id = NextJson::GetRequired<std::string>(relicJson, "id", context);
        relic.name = NextJson::GetRequired<std::string>(relicJson, "name", context);
        relic.icon = NextJson::GetRequired<std::string>(relicJson, "icon", context);
        relic.buffs = NextJson::GetRequired<std::vector<std::string>>(relicJson, "buffs", context);
        relic.statKey = NextJson::GetRequired<std::string>(relicJson, "statKey", context);
        relic.statVal = NextJson::GetRequired<float>(relicJson, "statVal", context);
        relic.color = NextJson::GetRequiredVec3(relicJson, "color", context);
        return relic;
    }
}

namespace KongLie3D
{
    std::map<std::string, FPieceDef> LoadPieces(const std::string& path)
    {
        const json document = NextJson::LoadFile(path);
        if (!document.contains("pieces") || !document.at("pieces").is_object())
        {
            DataLoaderLogAndThrow(fmt::format("pieces file '{}' is missing required object 'pieces'", path));
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
        const json document = NextJson::LoadFile(path);
        FPlacementData placement{};

        if (!document.contains("player") || !document.at("player").is_array())
        {
            DataLoaderLogAndThrow(fmt::format("placement file '{}' is missing required array 'player'", path));
        }
        if (!document.contains("levels") || !document.at("levels").is_array())
        {
            DataLoaderLogAndThrow(fmt::format("placement file '{}' is missing required array 'levels'", path));
        }

        for (size_t index = 0; index < document.at("player").size(); ++index)
        {
            placement.player.push_back(
                ParsePlacementEntry(document.at("player").at(index), fmt::format("placement.player[{}]", index)));
        }

        for (size_t index = 0; index < document.at("levels").size(); ++index)
        {
            placement.levels.push_back(ParseLevelDef(document.at("levels").at(index), index));
        }

        return placement;
    }

    std::vector<FSynergyDef> LoadSynergies(const std::string& path)
    {
        const json document = NextJson::LoadFile(path);
        if (!document.contains("synergies") || !document.at("synergies").is_array())
        {
            DataLoaderLogAndThrow(fmt::format("synergies file '{}' is missing required array 'synergies'", path));
        }

        std::vector<FSynergyDef> synergies;
        synergies.reserve(document.at("synergies").size());
        for (size_t index = 0; index < document.at("synergies").size(); ++index)
        {
            synergies.push_back(ParseSynergyDef(document.at("synergies").at(index), index));
        }
        return synergies;
    }

    std::vector<FRelicDef> LoadRelics(const std::string& path)
    {
        const json document = NextJson::LoadFile(path);
        if (!document.contains("relics") || !document.at("relics").is_array())
        {
            DataLoaderLogAndThrow(fmt::format("relics file '{}' is missing required array 'relics'", path));
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
