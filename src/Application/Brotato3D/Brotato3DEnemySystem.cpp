#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Assets/Core/Node.h"
#include <spdlog/spdlog.h>

#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

void Brotato3DGameInstance::SpawnEnemy(const std::string& enemyId, const glm::vec3& worldPos)
{
    const auto defIt = enemyDefs_.find(enemyId);
    const auto visualIt = enemyVisuals_.find(enemyId);
    if (defIt == enemyDefs_.end() || visualIt == enemyVisuals_.end())
    {
        spdlog::warn("[Brotato3D] Unknown enemy id '{}'", enemyId);
        return;
    }

    const Brotato3D::FEnemyDef& def = defIt->second;
    const FEnemyVisualResource& visual = visualIt->second;
    const glm::vec3 spawnPos(worldPos.x, def.size.y * 0.5f, worldPos.z);
    auto reusableEnemy = std::find_if(enemies_.begin(), enemies_.end(),
                                      [&def](const Brotato3D::FEnemyRuntime& enemy)
                                      {
                                          return !enemy.alive && !enemy.fading && enemy.def == &def && enemy.node;
                                      });

    Brotato3D::FEnemyRuntime enemy{};
    enemy.def = &def;
    enemy.worldPos = spawnPos;
    enemy.radius = std::max(def.size.x, def.size.z) * 0.5f;
    enemy.currentHp = def.hp;
    enemy.maxHp = def.hp;
    enemy.alive = true;
    enemy.modelId = visual.modelId;
    enemy.materialId = visual.materialId;
    enemy.darkMaterialId = visual.darkMaterialId;
    enemy.hitFlashMaterialId = visual.hitFlashMaterialId;
    enemy.warningMaterialId = visual.warningMaterialId;
    enemy.phase2MaterialId = visual.phase2MaterialId;
    if (reusableEnemy != enemies_.end())
    {
        enemy.node = reusableEnemy->node;
        *reusableEnemy = enemy;
        NodeUtils::SetPrimaryMaterial(reusableEnemy->node, reusableEnemy->materialId);
        reusableEnemy->node->SetTranslation(reusableEnemy->worldPos);
        reusableEnemy->node->SetScale(glm::vec3(1.0f));
        NodeUtils::SetVisible(reusableEnemy->node, true);
        return;
    }

    enemy.node = SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_Enemy_{}_{}", enemyId, enemies_.size()), spawnPos, glm::vec3(1.0f),
                                  engine_->GetScene().GenerateInstanceId(), visual.modelId, visual.materialId);
    engine_->GetScene().AddNode(enemy.node);
    engine_->GetScene().MarkDirty();
    enemies_.push_back(enemy);
}

void Brotato3DGameInstance::UpdateEnemies(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& enemy : enemies_)
    {
        if (!enemy.alive)
        {
            if (enemy.fading)
            {
                enemy.deathFadeMs -= deltaMs;
                const float alpha = std::clamp(enemy.deathFadeMs / 500.0f, 0.0f, 1.0f);
                enemy.node->SetTranslation(enemy.worldPos + glm::vec3(0.0f, -0.3f * (1.0f - alpha), 0.0f));
                if (enemy.deathFadeMs <= 0.0f)
                {
                    enemy.fading = false;
                    NodeUtils::SetVisible(enemy.node, false);
                }
            }
            continue;
        }

        enemy.contactCooldownMs = std::max(0.0f, enemy.contactCooldownMs - deltaMs);
        enemy.rangedFireCooldownMs = std::max(0.0f, enemy.rangedFireCooldownMs - deltaMs);
        enemy.chargeCooldownMs = std::max(0.0f, enemy.chargeCooldownMs - deltaMs);
        enemy.healIntervalMs = std::max(0.0f, enemy.healIntervalMs - deltaMs);
        enemy.hitFlashRemainingMs = std::max(0.0f, enemy.hitFlashRemainingMs - deltaMs);

        if (enemy.def->boss.enabled && !enemy.bossPhase2Active &&
            enemy.currentHp <= static_cast<int>(std::round(enemy.maxHp * enemy.def->boss.phase2HpRatio)))
        {
            enemy.bossPhase2Active = true;
            StartScreenShake(250.0f, 3.0f);
        }

        glm::vec3 toPlayer = player_.worldPos - enemy.worldPos;
        toPlayer.y = 0.0f;
        const float playerDistance = glm::length(toPlayer);
        uint32_t activeMaterial = enemy.bossPhase2Active ? enemy.phase2MaterialId : enemy.materialId;
        if (enemy.hitFlashRemainingMs > 0.0f)
        {
            activeMaterial = enemy.hitFlashMaterialId;
        }
        else if ((enemy.def->charge.enabled && enemy.charging) ||
                 (enemy.def->bomb.enabled && enemy.bombFuseMs >= 0.0f))
        {
            activeMaterial = enemy.warningMaterialId;
        }
        NodeUtils::SetPrimaryMaterial(enemy.node, activeMaterial);

        if (enemy.def->bomb.enabled && enemy.bombFuseMs >= 0.0f)
        {
            enemy.bombFuseMs -= deltaMs;
            const float pulse = 1.0f + std::sin(runElapsedSec_ * 28.0f) * 0.1f;
            enemy.node->SetScale(glm::vec3(pulse));
            if (enemy.bombFuseMs <= 0.0f)
            {
                PushExplosionRing(enemy.worldPos, glm::vec4(1.0f, 0.18f, 0.10f, 1.0f), enemy.def->bomb.explosionRadius);
                SpawnTempLight(enemy.worldPos, glm::vec3(1.0f, 0.25f, 0.08f), 5.0f, 250.0f);
                StartScreenShake(220.0f, 3.5f);
                if (DistanceXZ(enemy.worldPos, player_.worldPos) <= enemy.def->bomb.explosionRadius)
                {
                    DamagePlayer(enemy.def->bomb.explosionDamage, 180.0f, 250.0f);
                }
                SpawnDeathDebris(enemy);
                enemy.alive = false;
                enemy.fading = false;
                NodeUtils::SetVisible(enemy.node, false);
                continue;
            }
            continue;
        }
        else if (enemy.def->bomb.enabled)
        {
            enemy.node->SetScale(glm::vec3(1.0f));
        }

        if (enemy.def->heal.enabled && enemy.healIntervalMs <= 0.0f)
        {
            Brotato3D::FEnemyRuntime* healTarget = nullptr;
            float healTargetDistance = FLT_MAX;
            for (auto& candidate : enemies_)
            {
                if (&candidate == &enemy || !candidate.alive || candidate.currentHp >= candidate.maxHp)
                {
                    continue;
                }
                const float distance = DistanceXZ(candidate.worldPos, enemy.worldPos);
                if (distance <= enemy.def->heal.radiusMeters && distance < healTargetDistance)
                {
                    healTargetDistance = distance;
                    healTarget = &candidate;
                }
            }
            if (healTarget)
            {
                healTarget->currentHp = std::min(healTarget->maxHp, healTarget->currentHp + enemy.def->heal.healAmount);
                PushFloatingText(healTarget->worldPos + glm::vec3(0.0f, 0.8f, 0.0f),
                                 fmt::format("+{}", enemy.def->heal.healAmount),
                                 glm::vec4(0.75f, 0.35f, 1.0f, 1.0f), 600.0f);
                PushLaserBeam(enemy.worldPos + glm::vec3(0.0f, enemy.def->size.y, 0.0f),
                              healTarget->worldPos + glm::vec3(0.0f, healTarget->def->size.y, 0.0f),
                              glm::vec4(0.75f, 0.25f, 1.0f, 1.0f), 150.0f, 0.12f);
            }
            enemy.healIntervalMs = enemy.def->heal.intervalMs;
        }

        if (playerDistance > 0.001f)
        {
            const glm::vec3 toPlayerDir = toPlayer / playerDistance;
            glm::vec3 moveDir = toPlayerDir;
            float moveSpeed = enemy.def->moveSpeed;
            if (enemy.bossPhase2Active)
            {
                moveSpeed *= enemy.def->boss.phase2MoveSpeedMult;
            }
            if (enemy.def->bomb.enabled && playerDistance <= enemy.def->bomb.triggerDistance)
            {
                enemy.bombFuseMs = enemy.def->bomb.fuseMs;
                continue;
            }
            if (enemy.def->charge.enabled)
            {
                if (!enemy.charging && enemy.chargeCooldownMs <= 0.0f && playerDistance <= enemy.def->charge.triggerDistance)
                {
                    enemy.charging = true;
                    enemy.chargeRampMs = enemy.def->charge.chargeRampSec * 1000.0f;
                    enemy.chargeCooldownMs = enemy.def->charge.cooldownMs;
                }
                if (enemy.charging)
                {
                    const float rampTotal = std::max(1.0f, enemy.def->charge.chargeRampSec * 1000.0f);
                    enemy.chargeRampMs -= deltaMs;
                    const float rampProgress = 1.0f - std::clamp(enemy.chargeRampMs / rampTotal, 0.0f, 1.0f);
                    moveSpeed *= glm::mix(1.0f, enemy.def->charge.chargeSpeedMult, rampProgress);
                    if (enemy.chargeRampMs < -800.0f)
                    {
                        enemy.charging = false;
                    }
                }
                else if (playerDistance > enemy.def->charge.triggerDistance)
                {
                    moveSpeed = 0.0f;
                }
            }
            if (enemy.def->ranged.enabled)
            {
                const float preferredDistance = enemy.def->ranged.preferredDistance;
                if (playerDistance < preferredDistance - 0.5f)
                {
                    moveDir = -toPlayerDir;
                    moveSpeed = enemy.def->moveSpeed * 0.7f;
                }
                else if (playerDistance <= preferredDistance + 0.5f)
                {
                    moveSpeed = 0.0f;
                }

                if (enemy.rangedFireCooldownMs <= 0.0f)
                {
                    SpawnEnemyProjectile(enemy, toPlayerDir);
                    enemy.rangedFireCooldownMs = enemy.def->ranged.intervalMs;
                }
            }

            if (moveSpeed > 0.0f)
            {
                enemy.worldPos += moveDir * moveSpeed * static_cast<float>(deltaSeconds);
                enemy.worldPos = ClampToArena(enemy.worldPos, enemy.radius);
                enemy.worldPos.y = enemy.def->size.y * 0.5f;
                enemy.node->SetTranslation(enemy.worldPos);
            }
        }

        if (DistanceXZ(enemy.worldPos, player_.worldPos) < enemy.radius + player_.radius && enemy.contactCooldownMs <= 0.0f)
        {
            int contactDamage = enemy.def->contactDamage;
            if (enemy.def->charge.enabled && enemy.charging)
            {
                contactDamage = static_cast<int>(std::round(contactDamage * enemy.def->charge.contactDamageMult));
                enemy.charging = false;
            }
            if (enemy.bossPhase2Active)
            {
                contactDamage = static_cast<int>(std::round(contactDamage * enemy.def->boss.phase2ContactDamageMult));
            }
            contactDamage = static_cast<int>(std::round(static_cast<float>(contactDamage) * Brotato3D::MasterDifficulty));
            enemy.contactCooldownMs = 600.0f;
            if (enemy.def->name == "Brute")
            {
                hitStopMs_ = 80.0f;
                appState_ = Brotato3D::EAppState::Hitstop;
            }
            DamagePlayer(contactDamage, 150.0f, 180.0f);
        }
    }
}

void Brotato3DGameInstance::KillEnemy(Brotato3D::FEnemyRuntime& enemy, bool dropLoot)
{
    if (!enemy.alive)
    {
        return;
    }
    enemy.alive = false;
    enemy.fading = true;
    enemy.deathFadeMs = dropLoot ? 500.0f : 400.0f;
    enemy.node->SetScale(glm::vec3(1.0f));
    NodeUtils::SetPrimaryMaterial(enemy.node, enemy.darkMaterialId);
    if (dropLoot)
    {
        ++killCount_;
        Brotato3D::PlayEnemyDeathSfx(enemy.def ? enemy.def->name : std::string{});
        SpawnDeathDebris(enemy);
        SpawnPickup(enemy.def->xpDrop, Brotato3D::EPickupKind::XP, enemy.worldPos);
        SpawnPickup(enemy.def->materialDrop, Brotato3D::EPickupKind::Material, enemy.worldPos);
        ProcessOnKillTriggers(enemy.worldPos);
    }
    if (enemy.def && enemy.def->boss.enabled)
    {
        StartScreenShake(800.0f, 5.0f);
        explosionRings_.push_back({enemy.worldPos, glm::vec4(1.0f, 0.72f, 0.18f, 1.0f), 800.0f, 800.0f, 4.0f});
        spdlog::info("[Brotato3D] [boss defeated]");
        EnterResult(false);
    }
}

void Brotato3DGameInstance::ClearAliveEnemies(bool dropLoot)
{
    for (auto& enemy : enemies_)
    {
        if (enemy.alive)
        {
            KillEnemy(enemy, dropLoot);
        }
    }
}

void Brotato3DGameInstance::SpawnDeathDebris(const Brotato3D::FEnemyRuntime& enemy)
{
    if (!enemy.def)
    {
        return;
    }

    int debrisCount = 3;
    if (enemy.def->boss.enabled)
    {
        debrisCount = 30;
    }
    else if (enemy.def->bomb.enabled)
    {
        debrisCount = 8;
        SpawnTempLight(enemy.worldPos, glm::vec3(1.0f, 0.34f, 0.08f), 5.0f, 180.0f);
    }
    else if (enemy.def->heal.enabled || enemy.def->name == "Brute")
    {
        debrisCount = 6;
    }

    std::uniform_real_distribution<float> horizontalDist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> speedDist(2.0f, 4.0f);
    for (int index = 0; index < debrisCount; ++index)
    {
        auto it = std::find_if(impactDebrisPool_.begin(), impactDebrisPool_.end(),
                               [](const Brotato3D::FImpactDebrisRuntime& debris)
                               {
                                   return !debris.active;
                               });
        if (it == impactDebrisPool_.end())
        {
            return;
        }

        glm::vec3 dir(horizontalDist(rng_), 0.9f, horizontalDist(rng_));
        if (glm::length(dir) < 0.001f)
        {
            dir = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        dir = glm::normalize(dir);
        it->active = true;
        it->worldPos = enemy.worldPos;
        it->worldPos.y = std::max(0.35f, it->worldPos.y);
        it->velocity = dir * speedDist(rng_);
        it->lifeMs = enemy.def->boss.enabled ? 900.0f : 520.0f;
        it->remainingMs = it->lifeMs;
        it->node->SetTranslation(it->worldPos);
        NodeUtils::SetVisible(it->node, true);
    }
}


