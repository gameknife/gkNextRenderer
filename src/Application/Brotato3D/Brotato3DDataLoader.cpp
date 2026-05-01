#include "Brotato3DDataLoader.hpp"

#include "Utilities/FileHelper.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace
{
    bool LoadJsonFile(const std::string& path, json& outDocument)
    {
        const std::string absolutePath = Utilities::FileHelper::GetPlatformFilePath(path.c_str());
        std::ifstream input(absolutePath);
        if (!input.is_open())
        {
            SPDLOG_ERROR("[Brotato3D] Failed to open JSON file: {}", absolutePath);
            return false;
        }

        try
        {
            input >> outDocument;
            return true;
        }
        catch (const std::exception& exception)
        {
            SPDLOG_ERROR("[Brotato3D] Failed to parse JSON file {}: {}", absolutePath, exception.what());
            return false;
        }
    }

    bool RequireObject(const json& document, const char* key, const std::string& path)
    {
        if (!document.contains(key) || !document.at(key).is_object())
        {
            SPDLOG_ERROR("[Brotato3D] {} missing required object '{}'", path, key);
            return false;
        }
        return true;
    }

    bool RequireArray(const json& document, const char* key, const std::string& path)
    {
        if (!document.contains(key) || !document.at(key).is_array())
        {
            SPDLOG_ERROR("[Brotato3D] {} missing required array '{}'", path, key);
            return false;
        }
        return true;
    }

    glm::vec3 ReadVec3(const json& object, const char* key, const glm::vec3& defaultValue)
    {
        if (!object.contains(key) || !object.at(key).is_array() || object.at(key).size() != 3)
        {
            return defaultValue;
        }
        return glm::vec3(object.at(key).at(0).get<float>(), object.at(key).at(1).get<float>(),
                         object.at(key).at(2).get<float>());
    }
}

namespace Brotato3D
{
    bool LoadEnemies(const std::string& path, std::map<std::string, FEnemyDef>& outEnemies)
    {
        json document;
        if (!LoadJsonFile(path, document) || !RequireObject(document, "enemies", path))
        {
            return false;
        }

        outEnemies.clear();
        for (const auto& [enemyId, enemyJson] : document.at("enemies").items())
        {
            if (!enemyJson.contains("name") || !enemyJson.contains("hp") || !enemyJson.contains("moveSpeed") ||
                !enemyJson.contains("contactDamage"))
            {
                SPDLOG_ERROR("[Brotato3D] enemy '{}' missing required combat fields", enemyId);
                return false;
            }

            FEnemyDef def{};
            def.name = enemyJson.value("name", enemyId);
            def.hp = enemyJson.value("hp", 1);
            def.moveSpeed = enemyJson.value("moveSpeed", 1.0f);
            def.contactDamage = enemyJson.value("contactDamage", 1);
            def.size = ReadVec3(enemyJson, "size", glm::vec3(0.5f));
            def.color = ReadVec3(enemyJson, "color", glm::vec3(1.0f));
            def.xpDrop = enemyJson.value("xpDrop", 1);
            def.materialDrop = enemyJson.value("materialDrop", 1);
            def.kitingDistance = enemyJson.value("kitingDistance", 0.0f);
            outEnemies.emplace(enemyId, def);
        }
        return true;
    }

    bool LoadWeapons(const std::string& path, std::map<std::string, FWeaponDef>& outWeapons)
    {
        json document;
        if (!LoadJsonFile(path, document) || !RequireObject(document, "weapons", path))
        {
            return false;
        }

        outWeapons.clear();
        for (const auto& [weaponId, weaponJson] : document.at("weapons").items())
        {
            if (!weaponJson.contains("name") || !weaponJson.contains("damage") || !weaponJson.contains("atkSpeedHz"))
            {
                SPDLOG_ERROR("[Brotato3D] weapon '{}' missing required combat fields", weaponId);
                return false;
            }

            FWeaponDef def{};
            def.name = weaponJson.value("name", weaponId);
            def.damage = weaponJson.value("damage", 1);
            def.atkSpeedHz = weaponJson.value("atkSpeedHz", 1.0f);
            def.rangeMeters = weaponJson.value("rangeMeters", 1.0f);
            def.projectileSpeed = weaponJson.value("projectileSpeed", 10.0f);
            def.projectileLifetimeMs = weaponJson.value("projectileLifetimeMs", 500.0f);
            def.projectileColor = ReadVec3(weaponJson, "projectileColor", glm::vec3(1.0f));
            def.projectileSize = weaponJson.value("projectileSize", 0.12f);
            def.pellets = weaponJson.value("pellets", 1);
            def.spreadDeg = weaponJson.value("spreadDeg", 0.0f);
            outWeapons.emplace(weaponId, def);
        }
        return true;
    }

    bool LoadUpgrades(const std::string& path, std::vector<FUpgradeCardDef>& outCards)
    {
        json document;
        if (!LoadJsonFile(path, document) || !RequireArray(document, "cards", path))
        {
            return false;
        }

        outCards.clear();
        for (const auto& cardJson : document.at("cards"))
        {
            FUpgradeCardDef def{};
            def.id = cardJson.value("id", "");
            def.name = cardJson.value("name", def.id);
            def.stat = cardJson.value("stat", "");
            def.delta = cardJson.value("delta", 0.0f);
            def.weight = cardJson.value("weight", 1);
            if (def.id.empty() || def.stat.empty())
            {
                SPDLOG_ERROR("[Brotato3D] upgrade card missing id/stat");
                return false;
            }
            outCards.push_back(def);
        }
        return true;
    }

    bool LoadShopItems(const std::string& path, std::vector<FShopItemDef>& outItems)
    {
        json document;
        if (!LoadJsonFile(path, document) || !RequireArray(document, "items", path))
        {
            return false;
        }

        outItems.clear();
        for (const auto& itemJson : document.at("items"))
        {
            FShopItemDef def{};
            def.id = itemJson.value("id", "");
            def.name = itemJson.value("name", def.id);
            def.stat = itemJson.value("stat", "");
            def.delta = itemJson.value("delta", 0.0f);
            def.cost = itemJson.value("cost", 0);
            def.weight = itemJson.value("weight", 1);
            if (def.id.empty() || def.stat.empty())
            {
                SPDLOG_ERROR("[Brotato3D] shop item missing id/stat");
                return false;
            }
            outItems.push_back(def);
        }
        return true;
    }

    bool LoadWaves(const std::string& path, std::vector<FWaveDef>& outWaves)
    {
        json document;
        if (!LoadJsonFile(path, document) || !RequireArray(document, "waves", path))
        {
            return false;
        }

        outWaves.clear();
        for (const auto& waveJson : document.at("waves"))
        {
            FWaveDef wave{};
            wave.durationSec = waveJson.value("durationSec", 30);
            if (!waveJson.contains("spawns") || !waveJson.at("spawns").is_array())
            {
                SPDLOG_ERROR("[Brotato3D] wave missing spawns array");
                return false;
            }
            for (const auto& spawnJson : waveJson.at("spawns"))
            {
                FSpawnEntry spawn{};
                spawn.enemyId = spawnJson.value("enemyId", "");
                spawn.count = spawnJson.value("count", 0);
                spawn.intervalMs = spawnJson.value("intervalMs", 1000.0f);
                if (spawn.enemyId.empty() || spawn.count <= 0)
                {
                    SPDLOG_ERROR("[Brotato3D] invalid wave spawn entry");
                    return false;
                }
                wave.spawns.push_back(spawn);
            }
            outWaves.push_back(wave);
        }
        return true;
    }
}
