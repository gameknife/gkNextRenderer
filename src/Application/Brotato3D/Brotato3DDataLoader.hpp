#pragma once

#include "Common/CoreMinimal.hpp"

#include <glm/ext.hpp>

namespace Brotato3D
{
    struct FEnemyDef
    {
        std::string name;
        int hp = 1;
        float moveSpeed = 1.0f;
        int contactDamage = 1;
        glm::vec3 size = glm::vec3(0.5f);
        glm::vec3 color = glm::vec3(1.0f);
        int xpDrop = 1;
        int materialDrop = 1;
        float kitingDistance = 0.0f;
    };

    struct FWeaponDef
    {
        std::string name;
        int damage = 1;
        float atkSpeedHz = 1.0f;
        float rangeMeters = 1.0f;
        float projectileSpeed = 10.0f;
        float projectileLifetimeMs = 500.0f;
        glm::vec3 projectileColor = glm::vec3(1.0f);
        float projectileSize = 0.12f;
        int pellets = 1;
        float spreadDeg = 0.0f;
    };

    struct FUpgradeCardDef
    {
        std::string id;
        std::string name;
        std::string stat;
        float delta = 0.0f;
        int weight = 1;
    };

    struct FShopItemDef
    {
        std::string id;
        std::string name;
        std::string stat;
        float delta = 0.0f;
        int cost = 0;
        int weight = 1;
    };

    struct FSpawnEntry
    {
        std::string enemyId;
        int count = 0;
        float intervalMs = 1000.0f;
    };

    struct FWaveDef
    {
        int durationSec = 30;
        std::vector<FSpawnEntry> spawns;
    };

    bool LoadEnemies(const std::string& path, std::map<std::string, FEnemyDef>& outEnemies);
    bool LoadWeapons(const std::string& path, std::map<std::string, FWeaponDef>& outWeapons);
    bool LoadUpgrades(const std::string& path, std::vector<FUpgradeCardDef>& outCards);
    bool LoadShopItems(const std::string& path, std::vector<FShopItemDef>& outItems);
    bool LoadWaves(const std::string& path, std::vector<FWaveDef>& outWaves);
}
