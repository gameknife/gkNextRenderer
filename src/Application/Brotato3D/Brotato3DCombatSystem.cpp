#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

void Brotato3DGameInstance::ApplyDamageToEnemy(Brotato3D::FEnemyRuntime& enemy, int damage, bool isCrit)
{
    if (!enemy.alive)
    {
        return;
    }

    enemy.currentHp -= damage;
    Brotato3D::PlayHitSfx(damage, isCrit);
    enemy.hitFlashRemainingMs = 80.0f;
    SetNodeMaterial(enemy.node, enemy.hitFlashMaterialId);
    PushFloatingText(enemy.worldPos + glm::vec3(0.0f, 0.8f, 0.0f),
                     isCrit ? fmt::format("!{}", damage) : fmt::format("-{}", damage),
                     isCrit ? glm::vec4(1.0f, 0.78f, 0.12f, 1.0f) : glm::vec4(1.0f, 0.25f, 0.18f, 1.0f),
                     600.0f,
                     isCrit ? 1.4f : 1.0f);
    if (enemy.currentHp <= 0)
    {
        KillEnemy(enemy, true);
    }
}

void Brotato3DGameInstance::ProcessItemTriggers(double deltaSeconds)
{
    ProcessTickItemTriggers(static_cast<float>(deltaSeconds * 1000.0));
    ProcessLowHpItemBuffs();
}

void Brotato3DGameInstance::ProcessTickItemTriggers(float deltaMs)
{
    itemTickAccumMs_ += deltaMs;
    while (itemTickAccumMs_ >= 1000.0f)
    {
        itemTickAccumMs_ -= 1000.0f;
        for (const std::string& itemId : player_.ownedItemIds)
        {
            const Brotato3D::FItemDef* item = GetItemDef(itemId);
            if (!item || item->trigger != "on_tick" || item->effect != "heal_per_sec")
            {
                continue;
            }
            itemHealRemainder_ += item->value;
        }

        const int healAmount = static_cast<int>(std::floor(itemHealRemainder_));
        if (healAmount > 0)
        {
            itemHealRemainder_ -= static_cast<float>(healAmount);
            const int oldHp = player_.currentHp;
            player_.currentHp = std::min(player_.maxHp, player_.currentHp + healAmount);
            if (player_.currentHp > oldHp)
            {
                PushFloatingText(player_.worldPos + glm::vec3(0.0f, 1.0f, 0.0f), fmt::format("+{} HP", player_.currentHp - oldHp),
                                 glm::vec4(0.35f, 1.0f, 0.55f, 1.0f), 600.0f);
            }
        }
    }
}

void Brotato3DGameInstance::ProcessLowHpItemBuffs()
{
    dynamicStatBuffs_.damagePct = 0.0f;
    dynamicStatBuffs_.damageFlat = 0.0f;
    dynamicStatBuffs_.atkSpeedPct = 0.0f;
    dynamicStatBuffs_.rangePct = 0.0f;
    dynamicStatBuffs_.moveSpeedPct = 0.0f;
    dynamicStatBuffs_.pickupRadiusPct = 0.0f;
    dynamicStatBuffs_.critChancePct = 0.0f;

    const float hpRatio = player_.maxHp > 0 ? static_cast<float>(player_.currentHp) / static_cast<float>(player_.maxHp) : 0.0f;
    for (const std::string& itemId : player_.ownedItemIds)
    {
        const Brotato3D::FItemDef* item = GetItemDef(itemId);
        if (!item || item->trigger != "low_hp_buff" || hpRatio >= item->threshold)
        {
            continue;
        }
        if (item->effect == "stat_damagePct")
        {
            dynamicStatBuffs_.damagePct += item->value;
        }
    }
}

void Brotato3DGameInstance::ProcessOnKillTriggers(const glm::vec3& worldPos)
{
    for (const std::string& itemId : player_.ownedItemIds)
    {
        const Brotato3D::FItemDef* item = GetItemDef(itemId);
        if (!item)
        {
            continue;
        }
        if (item->trigger == "on_kill" && item->effect == "heal")
        {
            const int oldHp = player_.currentHp;
            player_.currentHp = std::min(player_.maxHp, player_.currentHp + static_cast<int>(std::round(item->value)));
            if (player_.currentHp > oldHp)
            {
                PushFloatingText(player_.worldPos + glm::vec3(0.0f, 1.0f, 0.0f), "+1 HP", glm::vec4(0.35f, 1.0f, 0.55f, 1.0f),
                                 600.0f);
            }
        }
        else if (item->trigger == "on_kill_chance" && item->effect == "explosion")
        {
            std::uniform_real_distribution<float> chanceDist(0.0f, 1.0f);
            if (chanceDist(rng_) < item->value)
            {
                PushExplosionRing(worldPos, glm::vec4(1.0f, 0.58f, 0.18f, 1.0f), item->explosionRadius);
                SpawnTempLight(worldPos + glm::vec3(0.0f, 0.4f, 0.0f), glm::vec3(1.0f, 0.48f, 0.12f), 3.0f, 120.0f);
                ApplyItemExplosionDamage(worldPos, item->explosionRadius, item->explosionDamage);
            }
        }
    }
}

void Brotato3DGameInstance::ApplyItemExplosionDamage(const glm::vec3& worldPos, float radius, int damage)
{
    if (radius <= 0.0f || damage <= 0)
    {
        return;
    }

    for (auto& enemy : enemies_)
    {
        if (!enemy.alive || DistanceXZ(enemy.worldPos, worldPos) > radius)
        {
            continue;
        }

        enemy.currentHp -= damage;
        enemy.hitFlashRemainingMs = 80.0f;
        SetNodeMaterial(enemy.node, enemy.hitFlashMaterialId);
        PushFloatingText(enemy.worldPos + glm::vec3(0.0f, 0.8f, 0.0f), fmt::format("-{}", damage),
                         glm::vec4(1.0f, 0.5f, 0.18f, 1.0f), 600.0f);
        if (enemy.currentHp <= 0)
        {
            KillEnemy(enemy, false);
        }
    }
}

void Brotato3DGameInstance::PushFloatingText(const glm::vec3& worldPos,
                                             std::string text,
                                             const glm::vec4& color,
                                             float lifeMs,
                                             float fontScale)
{
    std::uniform_real_distribution<float> xDist(-0.15f, 0.15f);
    std::uniform_real_distribution<float> yDist(0.0f, 0.2f);
    glm::vec3 offsetPos = worldPos;
    offsetPos.x += xDist(rng_);
    offsetPos.y += yDist(rng_);
    floatingTexts_.push_back({offsetPos, std::move(text), color, lifeMs, lifeMs, fontScale});
}

