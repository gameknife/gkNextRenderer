#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include <spdlog/spdlog.h>

#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

void Brotato3DGameInstance::ApplyWeaponKnockback(Brotato3D::FEnemyRuntime& enemy,
                                                 const glm::vec3& direction,
                                                 float knockbackMeters)
{
    if (!enemy.alive || knockbackMeters <= 0.0f)
    {
        return;
    }

    glm::vec3 knockDir(direction.x, 0.0f, direction.z);
    if (glm::length(knockDir) < 0.001f)
    {
        return;
    }
    knockDir = glm::normalize(knockDir);

    float effectiveKnockback = knockbackMeters;
    if (enemy.def && enemy.def->boss.enabled)
    {
        effectiveKnockback *= 0.25f;
    }
    else if (enemy.def)
    {
        const float sizeScale = std::max(0.45f, std::max(enemy.def->size.x, enemy.def->size.z));
        effectiveKnockback /= std::sqrt(sizeScale / 0.45f);
    }

    if (effectiveKnockback <= 0.001f)
    {
        return;
    }

    enemy.worldPos = ResolveEnemyGroundedPosition(enemy, enemy.worldPos + knockDir * effectiveKnockback);
    if (enemy.node)
    {
        enemy.node->SetTranslation(enemy.worldPos);
    }
    // Knockback is an instant positional snap; use a stable fallback step so Jolt derives a sane push velocity.
    SyncEnemyKinematicBody(enemy, 1.0 / 60.0);
}

// Note: weapons fire at real-time even during boss-kill slow-motion (effectiveDt < 1.0).
// This intentional asymmetry lets the player keep shooting while bullets/enemies/debris crawl,
// emphasizing the slow-mo "savor the kill" beat. Don't "fix" this by passing effectiveDt.
void Brotato3DGameInstance::UpdateWeapons(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    const Brotato3D::FPlayerStats effectiveStats = GetEffectiveStats();
    for (auto& weapon : equippedWeapons_)
    {
        if (!weapon.def)
        {
            continue;
        }
        weapon.cooldownMs -= deltaMs;
        if (weapon.cooldownMs > 0.0f)
        {
            continue;
        }

        Brotato3D::FEnemyRuntime* target = nullptr;
        float bestDistance = FLT_MAX;
        const float range = weapon.def->rangeMeters * (1.0f + effectiveStats.rangePct);
        for (auto& enemy : enemies_)
        {
            if (!enemy.alive)
            {
                continue;
            }
            const float distance = DistanceXZ(enemy.worldPos, player_.worldPos);
            if (distance <= range &&
                distance < bestDistance &&
                !IsSegmentBlockedByExtractionVehicle(player_.worldPos, enemy.worldPos, 0.05f))
            {
                bestDistance = distance;
                target = &enemy;
            }
        }
        if (!target)
        {
            continue;
        }

        glm::vec3 dir = target->worldPos - player_.worldPos;
        dir.y = 0.0f;
        dir = glm::normalize(dir);

        bool isCrit = false;
        const int weaponDamage = CalculateWeaponDamage(*weapon.def, isCrit);
        const glm::vec3 muzzlePos = player_.worldPos + dir * 0.6f + glm::vec3(0.0f, 0.4f, 0.0f);
        Brotato3D::PlayWeaponFireSfx(weapon.weaponId);
        PushMuzzleFlash(muzzlePos, weapon.def->projectileColor);
        SpawnTempLight(muzzlePos, weapon.def->projectileColor, 3.0f, 120.0f);
        if (weapon.def->instantHit)
        {
            const glm::vec3 hitPos = target->worldPos + glm::vec3(0.0f, target->def ? target->def->size.y * 0.5f : 0.4f, 0.0f);
            target->lastHitDebrisDir = dir;
            ApplyWeaponKnockback(*target, dir, weapon.def->knockbackMeters);
            const uint32_t hitMaterialId = target->bossPhase2Active && target->phase2MaterialId != 0 ? target->phase2MaterialId :
                                                                                                        target->materialId;
            const int effectiveDamage = ApplyDamageToEnemy(*target, weaponDamage, isCrit);
            SpawnHitXpDebris(hitPos, dir, effectiveDamage, hitMaterialId);
            PushLaserBeam(player_.worldPos + glm::vec3(0.0f, 0.25f, 0.0f),
                          hitPos,
                          glm::vec4(weapon.def->projectileColor, 1.0f),
                          std::max(1.0f, weapon.def->beamDurationMs),
                          std::max(0.02f, weapon.def->beamWidth));
            weapon.cooldownMs = 1000.0f / std::max(0.01f, weapon.def->atkSpeedHz * (1.0f + effectiveStats.atkSpeedPct));
            continue;
        }

        std::uniform_real_distribution<float> spreadDist(-weapon.def->spreadDeg * 0.5f, weapon.def->spreadDeg * 0.5f);
        const int pelletCount = std::max(1, weapon.def->pellets);
        for (int pelletIndex = 0; pelletIndex < pelletCount; ++pelletIndex)
        {
            const float spreadRadians = glm::radians(spreadDist(rng_));
            const glm::vec3 projectileDir = RotateY(dir, spreadRadians);
            for (auto& projectile : projectilePool_)
            {
                if (projectile.active || projectile.weaponId != weapon.weaponId)
                {
                    continue;
                }
                projectile.active = true;
                projectile.worldPos = player_.worldPos + projectileDir * 0.5f;
                projectile.worldPos.y = 0.5f;
                projectile.lastWorldPos = projectile.worldPos;
                projectile.velocity = projectileDir * weapon.def->projectileSpeed;
                projectile.color = weapon.def->projectileColor;
                projectile.remainingLifetimeMs = weapon.def->projectileLifetimeMs;
                projectile.elapsedMs = 0.0f;
                projectile.damage = weaponDamage;
                projectile.radius = weapon.def->projectileSize;
                projectile.pierceRemaining = weapon.def->pierceCount;
                projectile.explosionRadius = weapon.def->explosionRadius;
                projectile.explosionDamage = weapon.def->explosionDamage;
                projectile.knockbackMeters = weapon.def->knockbackMeters;
                projectile.isCrit = isCrit;
                projectile.hitEnemyIndices.clear();
                projectile.node->SetTranslation(projectile.worldPos);
                Assets::NodeUtils::SetVisible(projectile.node, true);
                break;
            }
        }

        weapon.cooldownMs = 1000.0f / std::max(0.01f, weapon.def->atkSpeedHz * (1.0f + effectiveStats.atkSpeedPct));
    }
}

void Brotato3DGameInstance::UpdateProjectiles(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& projectile : projectilePool_)
    {
        if (!projectile.active)
        {
            continue;
        }

        projectile.lastWorldPos = projectile.worldPos;
        projectile.worldPos += projectile.velocity * static_cast<float>(deltaSeconds);
        projectile.remainingLifetimeMs -= deltaMs;
        projectile.elapsedMs += deltaMs;
        bool deactivate = projectile.remainingLifetimeMs <= 0.0f ||
                          std::abs(projectile.worldPos.x) > arenaHalfExtent_.x + 1.0f ||
                          std::abs(projectile.worldPos.z) > arenaHalfExtent_.y + 1.0f;

        if (!deactivate &&
            IsSegmentBlockedByExtractionVehicle(projectile.lastWorldPos, projectile.worldPos, projectile.radius))
        {
            if (projectile.explosionRadius > 0.0f && projectile.explosionDamage > 0)
            {
                PushExplosionRing(projectile.worldPos, glm::vec4(1.0f, 0.55f, 0.12f, 1.0f), projectile.explosionRadius);
                SpawnTempLight(projectile.worldPos, glm::vec3(1.0f, 0.45f, 0.10f), 5.0f, 250.0f);
                StartScreenShake(180.0f, 2.8f);
            }
            else
            {
                PushMuzzleFlash(projectile.worldPos, projectile.color);
            }
            deactivate = true;
        }

        if (!deactivate)
        {
            for (size_t enemyIndex = 0; enemyIndex < enemies_.size(); ++enemyIndex)
            {
                auto& enemy = enemies_[enemyIndex];
                if (!enemy.alive)
                {
                    continue;
                }
                if (projectile.hitEnemyIndices.contains(enemyIndex))
                {
                    continue;
                }
                if (DistanceXZ(projectile.worldPos, enemy.worldPos) < enemy.radius + projectile.radius)
                {
                    if (projectile.explosionRadius > 0.0f && projectile.elapsedMs < 100.0f)
                    {
                        continue;
                    }

                    projectile.hitEnemyIndices.insert(enemyIndex);
                    if (glm::length(projectile.velocity) > 0.001f)
                    {
                        enemy.lastHitDebrisDir = glm::normalize(glm::vec3(projectile.velocity.x, 0.0f, projectile.velocity.z));
                    }
                    ApplyWeaponKnockback(enemy, projectile.velocity, projectile.knockbackMeters);
                    const uint32_t hitMaterialId = enemy.bossPhase2Active && enemy.phase2MaterialId != 0 ? enemy.phase2MaterialId :
                                                                                                           enemy.materialId;
                    const int effectiveDamage = ApplyDamageToEnemy(enemy, projectile.damage, projectile.isCrit);
                    const glm::vec3 hitDir = glm::length(projectile.velocity) > 0.001f ? projectile.velocity : enemy.worldPos - player_.worldPos;
                    SpawnHitXpDebris(projectile.worldPos, hitDir, effectiveDamage, hitMaterialId);

                    if (projectile.explosionRadius > 0.0f && projectile.explosionDamage > 0)
                    {
                        PushExplosionRing(projectile.worldPos, glm::vec4(1.0f, 0.55f, 0.12f, 1.0f),
                                          projectile.explosionRadius);
                        SpawnTempLight(projectile.worldPos, glm::vec3(1.0f, 0.45f, 0.10f), 5.0f, 250.0f);
                        StartScreenShake(200.0f, 3.5f);
                        for (auto& aoeEnemy : enemies_)
                        {
                            if (!aoeEnemy.alive)
                            {
                                continue;
                            }
                            if (DistanceXZ(projectile.worldPos, aoeEnemy.worldPos) <= projectile.explosionRadius)
                            {
                                const glm::vec3 blastDir = aoeEnemy.worldPos - projectile.worldPos;
                                if (glm::length(blastDir) > 0.001f)
                                {
                                    aoeEnemy.lastHitDebrisDir = glm::normalize(glm::vec3(blastDir.x, 0.0f, blastDir.z));
                                }
                                else if (glm::length(projectile.velocity) > 0.001f)
                                {
                                    aoeEnemy.lastHitDebrisDir = glm::normalize(glm::vec3(projectile.velocity.x, 0.0f, projectile.velocity.z));
                                }
                                ApplyWeaponKnockback(
                                    aoeEnemy,
                                    glm::length(blastDir) > 0.001f ? blastDir : projectile.velocity,
                                    projectile.knockbackMeters * 0.65f);
                                const uint32_t aoeHitMaterialId = aoeEnemy.bossPhase2Active && aoeEnemy.phase2MaterialId != 0 ?
                                    aoeEnemy.phase2MaterialId :
                                    aoeEnemy.materialId;
                                const int effectiveAoeDamage = ApplyDamageToEnemy(aoeEnemy, projectile.explosionDamage, false);
                                const glm::vec3 impactDir = glm::length(blastDir) > 0.001f ? blastDir : projectile.velocity;
                                SpawnHitXpDebris(aoeEnemy.worldPos + glm::vec3(0.0f, aoeEnemy.def ? aoeEnemy.def->size.y * 0.45f : 0.35f, 0.0f),
                                                 impactDir,
                                                 effectiveAoeDamage,
                                                 aoeHitMaterialId);
                            }
                        }
                        deactivate = true;
                    }
                    else
                    {
                        --projectile.pierceRemaining;
                        deactivate = projectile.pierceRemaining < 0;
                    }
                    break;
                }
            }
        }

        if (deactivate)
        {
            if (projectile.weaponId == "flamethrower")
            {
                PushFloatingText(projectile.worldPos, ".", glm::vec4(0.92f, 0.92f, 0.86f, 0.75f), 400.0f, 1.2f);
            }
            projectile.active = false;
            projectile.worldPos = HiddenPosition;
            projectile.hitEnemyIndices.clear();
            projectile.node->SetTranslation(HiddenPosition);
            Assets::NodeUtils::SetVisible(projectile.node, false);
        }
        else
        {
            projectile.node->SetTranslation(projectile.worldPos);
        }
    }
}

void Brotato3DGameInstance::SpawnEnemyProjectile(const Brotato3D::FEnemyRuntime& enemy, const glm::vec3& dir)
{
    if (!enemy.def || !enemy.def->ranged.enabled)
    {
        return;
    }

    auto projectileIt = std::find_if(enemyProjectilePool_.begin(), enemyProjectilePool_.end(),
                                     [](const Brotato3D::FEnemyProjectileRuntime& projectile)
                                     {
                                         return !projectile.active;
                                     });
    if (projectileIt == enemyProjectilePool_.end())
    {
        projectileIt = enemyProjectilePool_.begin();
    }
    if (projectileIt == enemyProjectilePool_.end())
    {
        return;
    }

    const Brotato3D::FEnemyRangedDef& ranged = enemy.def->ranged;
    projectileIt->active = true;
    projectileIt->worldPos = enemy.worldPos + glm::vec3(0.0f, enemy.def->size.y * 0.5f, 0.0f);
    projectileIt->velocity = dir * ranged.speed;
    projectileIt->remainingLifetimeMs = ranged.lifetimeMs;
    projectileIt->damage = static_cast<int>(std::round(static_cast<float>(ranged.dmg) * Brotato3D::MasterDifficulty));
    projectileIt->radius = ranged.size;
    if (const auto materialIt = enemyProjectileMaterialIds_.find(enemy.def); materialIt != enemyProjectileMaterialIds_.end())
    {
        Assets::NodeUtils::SetPrimaryMaterial(projectileIt->node, materialIt->second);
    }
    projectileIt->node->SetScale(glm::vec3(ranged.size));
    projectileIt->node->SetTranslation(projectileIt->worldPos);
    Assets::NodeUtils::SetOutlineFlags(projectileIt->node, Runtime::RenderOutlineFlags::danger);
    Assets::NodeUtils::SetVisible(projectileIt->node, true);
}

void Brotato3DGameInstance::UpdateEnemyProjectiles(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& projectile : enemyProjectilePool_)
    {
        if (!projectile.active)
        {
            continue;
        }

        const glm::vec3 previousPos = projectile.worldPos;
        projectile.worldPos += projectile.velocity * static_cast<float>(deltaSeconds);
        projectile.remainingLifetimeMs -= deltaMs;
        bool deactivate = projectile.remainingLifetimeMs <= 0.0f ||
                          std::abs(projectile.worldPos.x) > arenaHalfExtent_.x + 1.0f ||
                          std::abs(projectile.worldPos.z) > arenaHalfExtent_.y + 1.0f;

        if (!deactivate &&
            IsSegmentBlockedByExtractionVehicle(previousPos, projectile.worldPos, projectile.radius))
        {
            PushMuzzleFlash(projectile.worldPos, glm::vec3(0.55f, 0.95f, 0.20f));
            deactivate = true;
        }

        if (!deactivate && DistanceXZ(projectile.worldPos, player_.worldPos) < player_.radius + projectile.radius)
        {
            deactivate = true;
            DamagePlayer(projectile.damage, 120.0f, 250.0f);
        }

        if (deactivate)
        {
            projectile.active = false;
            projectile.worldPos = HiddenPosition;
            projectile.node->SetTranslation(HiddenPosition);
            Assets::NodeUtils::SetVisible(projectile.node, false);
        }
        else
        {
            projectile.node->SetTranslation(projectile.worldPos);
        }
    }
}

int Brotato3DGameInstance::CalculateWeaponDamage(const Brotato3D::FWeaponDef& weaponDef, bool& outIsCrit)
{
    const Brotato3D::FPlayerStats effectiveStats = GetEffectiveStats();
    std::uniform_real_distribution<float> critDist(0.0f, 1.0f);
    const float critChance = std::clamp(effectiveStats.critChancePct + weaponDef.critChanceBonus, 0.0f, 1.0f);
    outIsCrit = critDist(rng_) < critChance;
    int damage = static_cast<int>(std::round(weaponDef.damage * (1.0f + effectiveStats.damagePct) + effectiveStats.damageFlat));
    if (outIsCrit)
    {
        damage = static_cast<int>(std::round(static_cast<float>(damage) * effectiveStats.critMultiplier));
    }
    return std::max(0, damage);
}

Brotato3D::FWeaponRuntime Brotato3DGameInstance::CreateWeaponRuntime(const std::string& weaponId, int tier) const
{
    Brotato3D::FWeaponRuntime runtime{};
    const auto weaponIt = weaponDefs_.find(weaponId);
    if (weaponIt == weaponDefs_.end())
    {
        return runtime;
    }

    runtime.weaponId = weaponId;
    runtime.tier = tier;
    runtime.tieredDef = tier == 2 ? CreateTier2Weapon(weaponIt->second) : weaponIt->second;
    runtime.tieredDef.tier = tier;
    runtime.def = &runtime.tieredDef;
    runtime.cooldownMs = 0.0f;
    return runtime;
}

Brotato3D::FWeaponDef Brotato3DGameInstance::CreateTier2Weapon(const Brotato3D::FWeaponDef& base) const
{
    Brotato3D::FWeaponDef tierTwo = base;
    tierTwo.tier = 2;
    tierTwo.damage = static_cast<int>(std::round(static_cast<float>(base.damage) * 1.5f));
    tierTwo.atkSpeedHz *= 1.2f;
    tierTwo.knockbackMeters *= 1.15f;
    tierTwo.name += " ★";
    return tierTwo;
}

void Brotato3DGameInstance::NormalizeWeaponDefPointers()
{
    for (auto& weapon : equippedWeapons_)
    {
        weapon.def = &weapon.tieredDef;
        weapon.tier = weapon.tieredDef.tier;
    }
}

bool Brotato3DGameInstance::CanBuyWeaponCard(const std::string& weaponId) const
{
    if (equippedWeapons_.size() < MaxWeaponSlots)
    {
        return true;
    }

    int sameTierOneCount = 0;
    for (const Brotato3D::FWeaponRuntime& weapon : equippedWeapons_)
    {
        if (weapon.weaponId == weaponId && weapon.tier == 1)
        {
            ++sameTierOneCount;
        }
    }
    return sameTierOneCount >= 1;
}

bool Brotato3DGameInstance::BuyWeaponCard(const Brotato3D::FShopItemDef& item)
{
    if (!CanBuyWeaponCard(item.weaponId))
    {
        return false;
    }

    Brotato3D::FWeaponRuntime runtime = CreateWeaponRuntime(item.weaponId, 1);
    if (!runtime.def)
    {
        return false;
    }

    player_.materials -= item.cost;
    equippedWeapons_.push_back(runtime);
    NormalizeWeaponDefPointers();
    TryMergeWeapons(item.weaponId);
    return true;
}

void Brotato3DGameInstance::TryMergeWeapons(const std::string& weaponId)
{
    std::vector<size_t> tierOneIndices;
    for (size_t index = 0; index < equippedWeapons_.size(); ++index)
    {
        const Brotato3D::FWeaponRuntime& weapon = equippedWeapons_[index];
        if (weapon.weaponId == weaponId && weapon.tier == 1)
        {
            tierOneIndices.push_back(index);
        }
    }
    if (tierOneIndices.size() < 2)
    {
        NormalizeWeaponDefPointers();
        return;
    }

    for (int removeIndex = 1; removeIndex >= 0; --removeIndex)
    {
        equippedWeapons_.erase(equippedWeapons_.begin() + static_cast<std::ptrdiff_t>(tierOneIndices[removeIndex]));
    }
    equippedWeapons_.push_back(CreateWeaponRuntime(weaponId, 2));
    NormalizeWeaponDefPointers();

    const Brotato3D::FWeaponDef* mergedWeapon = equippedWeapons_.back().def;
    const std::string baseName = mergedWeapon ? std::string(mergedWeapon->name).substr(0, mergedWeapon->name.find(" ★")) : weaponId;
    weaponMergeBannerText_ = fmt::format("⚡ {} TIER 2", baseName);
    weaponMergeBannerMs_ = 1500.0f;
    StartScreenShake(180.0f, 2.5f);
    Brotato3D::PlayLevelUpSfx();
}

void Brotato3DGameInstance::SetDebugSingleWeapon(const std::string& weaponId)
{
    const auto weaponIt = weaponDefs_.find(weaponId);
    if (weaponIt == weaponDefs_.end())
    {
        spdlog::warn("[Brotato3D] debug weapon '{}' not found", weaponId);
        return;
    }

    equippedWeapons_.clear();
    Brotato3D::FWeaponRuntime runtime = CreateWeaponRuntime(weaponId, 1);
    if (runtime.def)
    {
        equippedWeapons_.push_back(runtime);
        NormalizeWeaponDefPointers();
    }
    spdlog::info("[Brotato3D] debug equipped weapon '{}'", weaponId);
}


