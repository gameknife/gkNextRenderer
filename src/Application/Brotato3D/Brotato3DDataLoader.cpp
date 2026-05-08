#include "Brotato3DDataLoader.hpp"

#include "Runtime/Utilities/JsonHelpers.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace
{
    void ReadPlayerStats(const json& object, Brotato3D::FPlayerStats& outStats)
    {
        outStats.maxHpFlat = object.value("maxHpFlat", outStats.maxHpFlat);
        outStats.damagePct = object.value("damagePct", outStats.damagePct);
        outStats.damageFlat = object.value("damageFlat", outStats.damageFlat);
        outStats.atkSpeedPct = object.value("atkSpeedPct", outStats.atkSpeedPct);
        outStats.rangePct = object.value("rangePct", outStats.rangePct);
        outStats.moveSpeedPct = object.value("moveSpeedPct", outStats.moveSpeedPct);
        outStats.pickupRadiusPct = object.value("pickupRadiusPct", outStats.pickupRadiusPct);
        outStats.critChancePct = object.value("critChancePct", outStats.critChancePct);
        outStats.critMultiplier = object.value("critMultiplier", outStats.critMultiplier);
    }
}

namespace Brotato3D
{
    bool LoadEnemies(const std::string& path, std::map<std::string, FEnemyDef>& outEnemies)
    {
        json document;
        if (!NextJson::TryLoadFile(path, document) || !NextJson::HasObject(document, "enemies"))
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
            def.size = NextJson::GetVec3(enemyJson, "size", glm::vec3(0.5f));
            def.color = NextJson::GetVec3(enemyJson, "color", glm::vec3(1.0f));
            def.xpDrop = enemyJson.value("xpDrop", 1);
            def.materialDrop = enemyJson.value("materialDrop", 1);
            def.kitingDistance = enemyJson.value("kitingDistance", 0.0f);
            if (enemyJson.contains("ranged") && enemyJson.at("ranged").is_object())
            {
                const json& rangedJson = enemyJson.at("ranged");
                def.ranged.enabled = true;
                def.ranged.dmg = rangedJson.value("projectileDamage", 0);
                def.ranged.speed = rangedJson.value("projectileSpeed", 0.0f);
                def.ranged.lifetimeMs = rangedJson.value("projectileLifetimeMs", 0.0f);
                def.ranged.color = NextJson::GetVec3(rangedJson, "projectileColor", glm::vec3(0.3f, 0.95f, 0.2f));
                def.ranged.size = rangedJson.value("projectileSize", 0.18f);
                def.ranged.intervalMs = rangedJson.value("fireIntervalMs", 0.0f);
                def.ranged.preferredDistance = rangedJson.value("preferredDistance", def.kitingDistance);
            }
            if (enemyJson.contains("charge") && enemyJson.at("charge").is_object())
            {
                const json& chargeJson = enemyJson.at("charge");
                def.charge.enabled = true;
                def.charge.triggerDistance = chargeJson.value("triggerDistance", 0.0f);
                def.charge.chargeSpeedMult = chargeJson.value("chargeSpeedMult", 1.0f);
                def.charge.chargeRampSec = chargeJson.value("chargeRampSec", 0.0f);
                def.charge.contactDamageMult = chargeJson.value("contactDamageMult", 1.0f);
                def.charge.cooldownMs = chargeJson.value("cooldownMs", 0.0f);
            }
            if (enemyJson.contains("bomb") && enemyJson.at("bomb").is_object())
            {
                const json& bombJson = enemyJson.at("bomb");
                def.bomb.enabled = true;
                def.bomb.triggerDistance = bombJson.value("triggerDistance", 0.0f);
                def.bomb.fuseMs = bombJson.value("fuseMs", 0.0f);
                def.bomb.explosionRadius = bombJson.value("explosionRadius", 0.0f);
                def.bomb.explosionDamage = bombJson.value("explosionDamage", 0);
            }
            if (enemyJson.contains("heal") && enemyJson.at("heal").is_object())
            {
                const json& healJson = enemyJson.at("heal");
                def.heal.enabled = true;
                def.heal.radiusMeters = healJson.value("radiusMeters", 0.0f);
                def.heal.healAmount = healJson.value("healAmount", 0);
                def.heal.intervalMs = healJson.value("intervalMs", 0.0f);
            }
            if (enemyJson.contains("boss") && enemyJson.at("boss").is_object())
            {
                const json& bossJson = enemyJson.at("boss");
                def.boss.enabled = true;
                def.boss.phase2HpRatio = bossJson.value("phase2HpRatio", 0.5f);
                def.boss.phase2MoveSpeedMult = bossJson.value("phase2MoveSpeedMult", 1.0f);
                def.boss.phase2ContactDamageMult = bossJson.value("phase2ContactDamageMult", 1.0f);
            }
            outEnemies.emplace(enemyId, def);
        }
        return true;
    }

    bool LoadWeapons(const std::string& path, std::map<std::string, FWeaponDef>& outWeapons)
    {
        json document;
        if (!NextJson::TryLoadFile(path, document) || !NextJson::HasObject(document, "weapons"))
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
            def.projectileColor = NextJson::GetVec3(weaponJson, "projectileColor", glm::vec3(1.0f));
            def.projectileSize = weaponJson.value("projectileSize", 0.12f);
            def.pellets = weaponJson.value("pellets", 1);
            def.spreadDeg = weaponJson.value("spreadDeg", 0.0f);
            def.pierceCount = weaponJson.value("pierceCount", 0);
            def.explosionRadius = weaponJson.value("explosionRadius", 0.0f);
            def.explosionDamage = weaponJson.value("explosionDamage", 0);
            def.instantHit = weaponJson.value("instantHit", false);
            def.beamWidth = weaponJson.value("beamWidth", 0.0f);
            def.beamDurationMs = weaponJson.value("beamDurationMs", 0.0f);
            def.critChanceBonus = weaponJson.value("critChanceBonus", 0.0f);
            def.knockbackMeters = weaponJson.value("knockbackMeters", 0.0f);
            def.tier = weaponJson.value("tier", 1);
            outWeapons.emplace(weaponId, def);
        }
        return true;
    }

    bool LoadUpgrades(const std::string& path, std::vector<FUpgradeCardDef>& outCards)
    {
        json document;
        if (!NextJson::TryLoadFile(path, document) || !NextJson::HasArray(document, "cards"))
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
        if (!NextJson::TryLoadFile(path, document) || !NextJson::HasArray(document, "items"))
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

    bool LoadItems(const std::string& path, std::vector<FItemDef>& outItems)
    {
        json document;
        if (!NextJson::TryLoadFile(path, document) || !NextJson::HasArray(document, "items"))
        {
            return false;
        }

        outItems.clear();
        for (const auto& itemJson : document.at("items"))
        {
            FItemDef def{};
            def.id = itemJson.value("id", "");
            def.name = itemJson.value("name", def.id);
            def.description = itemJson.value("description", "");
            def.trigger = itemJson.value("trigger", "");
            def.effect = itemJson.value("effect", "");
            def.value = itemJson.value("value", 0.0f);
            def.rarity = itemJson.value("rarity", "common");
            def.weight = itemJson.value("weight", 1);
            def.cost = itemJson.value("cost", 0);
            def.threshold = itemJson.value("threshold", 0.0f);
            def.explosionRadius = itemJson.value("explosionRadius", 0.0f);
            def.explosionDamage = itemJson.value("explosionDamage", 0);
            if (def.id.empty() || def.trigger.empty() || def.effect.empty())
            {
                SPDLOG_ERROR("[Brotato3D] item missing id/trigger/effect");
                return false;
            }
            outItems.push_back(def);
        }
        return true;
    }

    bool LoadCharacters(const std::string& path, std::vector<FCharacterDef>& outCharacters)
    {
        json document;
        if (!NextJson::TryLoadFile(path, document) || !NextJson::HasArray(document, "characters"))
        {
            return false;
        }

        outCharacters.clear();
        for (const auto& characterJson : document.at("characters"))
        {
            FCharacterDef def{};
            def.id = characterJson.value("id", "");
            def.name = characterJson.value("name", def.id);
            def.tagline = characterJson.value("tagline", "");
            def.color = NextJson::GetVec3(characterJson, "color", glm::vec3(1.0f));
            def.startWeapon = characterJson.value("startWeapon", "");
            def.startStats = FPlayerStats{};
            def.startStats.damagePct = 0.0f;
            def.startStats.damageFlat = 0.0f;
            def.startStats.atkSpeedPct = 0.0f;
            def.startStats.rangePct = 0.0f;
            def.startStats.moveSpeedPct = 0.0f;
            def.startStats.pickupRadiusPct = 0.0f;
            def.startStats.critChancePct = 0.0f;
            def.startStats.critMultiplier = 0.0f;
            if (characterJson.contains("startStats") && characterJson.at("startStats").is_object())
            {
                ReadPlayerStats(characterJson.at("startStats"), def.startStats);
            }
            if (def.id.empty() || def.startWeapon.empty())
            {
                SPDLOG_ERROR("[Brotato3D] character missing id/startWeapon");
                return false;
            }
            outCharacters.push_back(def);
        }
        return true;
    }

    bool LoadArenas(const std::string& path, std::vector<FArenaDef>& outArenas)
    {
        json document;
        if (!NextJson::TryLoadFile(path, document) || !NextJson::HasArray(document, "arenas"))
        {
            return false;
        }

        outArenas.clear();
        for (const auto& arenaJson : document.at("arenas"))
        {
            FArenaDef def{};
            def.id = arenaJson.value("id", "");
            def.name = arenaJson.value("name", def.id);
            def.groundColor = NextJson::GetVec3(arenaJson, "groundColor", def.groundColor);
            def.borderColor = NextJson::GetVec3(arenaJson, "borderColor", def.borderColor);
            if (def.id.empty())
            {
                SPDLOG_ERROR("[Brotato3D] arena missing id");
                return false;
            }
            outArenas.push_back(def);
        }
        return true;
    }

    bool LoadI18n(const std::string& path, std::map<std::string, std::string>& outTexts)
    {
        json document;
        if (!NextJson::TryLoadFile(path, document) || !NextJson::HasObject(document, "zh"))
        {
            return false;
        }

        outTexts.clear();
        for (const auto& [key, value] : document.at("zh").items())
        {
            if (value.is_string())
            {
                outTexts[key] = value.get<std::string>();
            }
        }
        return true;
    }

    bool LoadWaves(const std::string& path, std::vector<FWaveDef>& outWaves)
    {
        json document;
        if (!NextJson::TryLoadFile(path, document) || !NextJson::HasArray(document, "waves"))
        {
            return false;
        }

        outWaves.clear();
        for (const auto& waveJson : document.at("waves"))
        {
            FWaveDef wave{};
            wave.durationSec = waveJson.value("durationSec", 30);
            wave.bgmCue = waveJson.value("bgmCue", "");
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
