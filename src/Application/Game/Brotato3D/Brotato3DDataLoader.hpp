#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Brotato3DPcgConfig.hpp"
#include "Brotato3DPlayer.hpp"

#include <glm/ext.hpp>

namespace Brotato3D
{
    struct FEnemyRangedDef
    {
        int dmg = 0;
        float speed = 0.0f;
        float lifetimeMs = 0.0f;
        glm::vec3 color = glm::vec3(0.3f, 0.95f, 0.2f);
        float size = 0.18f;
        float intervalMs = 0.0f;
        float preferredDistance = 0.0f;
        bool enabled = false;
    };

    struct FMortarDef
    {
        float fireIntervalMs = 0.0f;
        float telegraphMs = 0.0f;
        float explosionRadius = 0.0f;
        int explosionDamage = 0;
        float throwRangeMin = 0.0f;
        float throwRangeMax = 0.0f;
        float lobHeightMeters = 0.0f;
        float leadFactor = 0.0f;
        bool enabled = false;
    };

    struct FLanceDef
    {
        float telegraphMs = 0.0f;
        float windupRangeMin = 0.0f;
        float dashSpeed = 0.0f;
        float dashDistanceMax = 0.0f;
        float dashContactDamageMult = 1.0f;
        float recoverMs = 0.0f;
        float cooldownMs = 0.0f;
        bool enabled = false;
    };

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
        FEnemyRangedDef ranged{};
        FMortarDef mortar{};
        FLanceDef lance{};
        struct FChargeDef
        {
            float triggerDistance = 0.0f;
            float chargeSpeedMult = 1.0f;
            float chargeRampSec = 0.0f;
            float contactDamageMult = 1.0f;
            float cooldownMs = 0.0f;
            bool enabled = false;
        } charge{};
        struct FBombDef
        {
            float triggerDistance = 0.0f;
            float fuseMs = 0.0f;
            float explosionRadius = 0.0f;
            int explosionDamage = 0;
            bool enabled = false;
        } bomb{};
        struct FHealDef
        {
            float radiusMeters = 0.0f;
            int healAmount = 0;
            float intervalMs = 0.0f;
            bool enabled = false;
        } heal{};
        struct FBossDef
        {
            float phase2HpRatio = 0.5f;
            float phase2MoveSpeedMult = 1.0f;
            float phase2ContactDamageMult = 1.0f;
            bool enabled = false;
        } boss{};
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
        int pierceCount = 0;
        float explosionRadius = 0.0f;
        int explosionDamage = 0;
        bool instantHit = false;
        float beamWidth = 0.0f;
        float beamDurationMs = 0.0f;
        float critChanceBonus = 0.0f;
        float knockbackMeters = 0.0f;
        int tier = 1;
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
        bool isPassiveItem = false;
        std::string description;
        std::string rarity;
        std::string trigger;
        std::string effect;
        float value = 0.0f;
        float threshold = 0.0f;
        float explosionRadius = 0.0f;
        int explosionDamage = 0;
        bool isWeaponCard = false;
        std::string weaponId;
    };

    struct FItemDef
    {
        std::string id;
        std::string name;
        std::string description;
        std::string trigger;
        std::string effect;
        std::string rarity;
        float value = 0.0f;
        float threshold = 0.0f;
        float explosionRadius = 0.0f;
        int explosionDamage = 0;
        int weight = 1;
        int cost = 0;
    };

    struct FCharacterDef
    {
        std::string id;
        std::string name;
        std::string tagline;
        glm::vec3 color = glm::vec3(1.0f);
        std::string startWeapon;
        FPlayerStats startStats{};
    };

    enum class EGroundMaterialKind : uint8_t
    {
        Lambertian = 0,
        Metallic = 1,
        Mixture = 2,
    };

    struct FGroundTileDef
    {
        glm::vec2 minXZ = glm::vec2(0.0f);
        glm::vec2 maxXZ = glm::vec2(0.0f);
        std::vector<glm::vec2> pointsXZ;
        glm::vec3 color = glm::vec3(0.5f);
        EGroundMaterialKind kind = EGroundMaterialKind::Lambertian;
        float coverage = 0.0f;
        float fuzziness = 0.1f;
        float refractionIndex = 1.45f;
    };

    struct FArenaDef
    {
        std::string id;
        std::string name;
        glm::vec2 halfExtent = glm::vec2(12.0f, 8.0f);
        glm::vec3 baseGroundColor = glm::vec3(0.32f, 0.40f, 0.28f);
        glm::vec3 borderColor = glm::vec3(0.45f, 0.55f, 0.35f);
        std::vector<FGroundTileDef> groundTiles;
        Pcg::FArenaPcgConfig pcg;
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
        std::string bgmCue;
        float duskSpawnMultiplier = 1.0f;
        float duskBonusXpMult = 1.0f;
        float extractionRequiredSec = 0.0f;
        float extractionRadiusM = 2.5f;
        std::vector<FSpawnEntry> spawns;
    };

    bool LoadEnemies(const std::string& path, std::map<std::string, FEnemyDef>& outEnemies);
    bool LoadWeapons(const std::string& path, std::map<std::string, FWeaponDef>& outWeapons);
    bool LoadUpgrades(const std::string& path, std::vector<FUpgradeCardDef>& outCards);
    bool LoadShopItems(const std::string& path, std::vector<FShopItemDef>& outItems);
    bool LoadItems(const std::string& path, std::vector<FItemDef>& outItems);
    bool LoadCharacters(const std::string& path, std::vector<FCharacterDef>& outCharacters);
    bool LoadArenas(const std::string& path, std::vector<FArenaDef>& outArenas);
    bool LoadWaves(const std::string& path, std::vector<FWaveDef>& outWaves);
}
