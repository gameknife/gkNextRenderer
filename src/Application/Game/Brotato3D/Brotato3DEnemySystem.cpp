#include "Brotato3DGameInstance.hpp"
#include "Brotato3DCommon.hpp"

#include "Engine/Assets/Core/Node.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include <spdlog/spdlog.h>

#include "Brotato3DAudio.hpp"

using namespace Brotato3DUtil;

namespace
{
    constexpr float BodyBlockFillScale = 1.02f;
    constexpr float BodyBlockMinExtent = 0.08f;

    glm::vec3 ResolveEnemyDebrisDir(const Brotato3D::FEnemyRuntime& enemy, const glm::vec3& fallbackDir)
    {
        glm::vec3 dir(enemy.lastHitDebrisDir.x, 0.0f, enemy.lastHitDebrisDir.z);
        if (glm::length(dir) < 0.001f)
        {
            dir = glm::vec3(fallbackDir.x, 0.0f, fallbackDir.z);
        }
        if (glm::length(dir) < 0.001f)
        {
            dir = glm::vec3(0.0f, 0.0f, 1.0f);
        }
        return glm::normalize(dir);
    }

    int CountBodyBlocksOnAxis(float size, bool boss, bool vertical)
    {
        if (boss)
        {
            return vertical ? 4 : 3;
        }
        const float threshold = vertical ? 0.65f : 0.75f;
        return size >= threshold ? 3 : 2;
    }
}

void Brotato3DGameInstance::CreateEnemyBodyBlocks(Brotato3D::FEnemyRuntime& enemy,
                                                  const FEnemyVisualResource& visual,
                                                  const std::string& enemyId)
{
    if (!enemy.node || !enemy.def || visual.bodyBlockModelId == 0)
    {
        return;
    }

    enemy.bodyBlocks.clear();
    enemy.bodyBlocksLost = 0;
    const bool boss = enemy.def->boss.enabled;
    const glm::ivec3 grid(CountBodyBlocksOnAxis(enemy.def->size.x, boss, false),
                          CountBodyBlocksOnAxis(enemy.def->size.y, boss, true),
                          CountBodyBlocksOnAxis(enemy.def->size.z, boss, false));
    const glm::vec3 cellSize = enemy.def->size / glm::vec3(grid);
    const glm::vec3 blockScale = glm::max(cellSize * BodyBlockFillScale, glm::vec3(BodyBlockMinExtent));
    auto& scene = GetEngine().GetScene();

    enemy.bodyBlocks.reserve(static_cast<size_t>(grid.x * grid.y * grid.z));
    for (int y = 0; y < grid.y; ++y)
    {
        for (int z = 0; z < grid.z; ++z)
        {
            for (int x = 0; x < grid.x; ++x)
            {
                const glm::vec3 localOffset = -enemy.def->size * 0.5f +
                    cellSize * (glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) + glm::vec3(0.5f));
                auto blockNode = SceneBuilder::CreateRenderNode(
                    fmt::format("Brotato3D_EnemyBlock_{}_{}_{}_{}", enemyId, enemy.runtimeTag, enemy.bodyBlocks.size(), enemy.def->name),
                    localOffset,
                    blockScale,
                    scene.GenerateInstanceId(),
                    visual.bodyBlockModelId,
                    enemy.materialId,
                    true);

                auto physicsComponent = std::make_shared<Runtime::PhysicsComponent>();
                physicsComponent->SetMobility(Runtime::ENodeMobility::Dynamic);
                blockNode->AddComponent(physicsComponent);
                blockNode->SetParent(enemy.node);
                NodeUtils::SetOutlineFlags(blockNode, Runtime::RenderOutlineFlags::danger);
                scene.AddNode(blockNode);

                Brotato3D::FEnemyBodyBlockRuntime block{};
                block.node = blockNode;
                block.localOffset = localOffset;
                block.visible = true;
                enemy.bodyBlocks.push_back(block);
            }
        }
    }
    NodeUtils::SetVisible(enemy.node, false);
    scene.MarkTransformDirty();
}

void Brotato3DGameInstance::ResetEnemyBodyBlocks(Brotato3D::FEnemyRuntime& enemy)
{
    enemy.bodyBlocksLost = 0;
    for (Brotato3D::FEnemyBodyBlockRuntime& block : enemy.bodyBlocks)
    {
        block.visible = true;
        if (block.node)
        {
            block.node->SetTranslation(block.localOffset);
            block.node->SetRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            NodeUtils::SetVisible(block.node, true);
        }
    }
    SetEnemyVisualMaterial(enemy, enemy.materialId);
    SetEnemyVisualOutlineFlags(enemy, Runtime::RenderOutlineFlags::danger);
    SetEnemyVisualVisible(enemy, true);
}

void Brotato3DGameInstance::SetEnemyVisualVisible(Brotato3D::FEnemyRuntime& enemy, bool visible)
{
    if (!enemy.node)
    {
        return;
    }

    NodeUtils::SetVisible(enemy.node, visible && enemy.bodyBlocks.empty());
    for (Brotato3D::FEnemyBodyBlockRuntime& block : enemy.bodyBlocks)
    {
        NodeUtils::SetVisible(block.node, visible && block.visible);
    }
}

void Brotato3DGameInstance::SetEnemyVisualMaterial(const Brotato3D::FEnemyRuntime& enemy, uint32_t materialId)
{
    NodeUtils::SetPrimaryMaterial(enemy.node, materialId);
    for (const Brotato3D::FEnemyBodyBlockRuntime& block : enemy.bodyBlocks)
    {
        NodeUtils::SetPrimaryMaterial(block.node, materialId);
    }
}

void Brotato3DGameInstance::SetEnemyVisualOutlineFlags(const Brotato3D::FEnemyRuntime& enemy, uint32_t outlineFlags)
{
    NodeUtils::SetOutlineFlags(enemy.node, outlineFlags);
    for (const Brotato3D::FEnemyBodyBlockRuntime& block : enemy.bodyBlocks)
    {
        NodeUtils::SetOutlineFlags(block.node, outlineFlags);
    }
}

void Brotato3DGameInstance::BreakEnemyBodyBlocks(Brotato3D::FEnemyRuntime& enemy, int damage)
{
    if (damage <= 0 || enemy.bodyBlocks.empty() || enemy.maxHp <= 0)
    {
        return;
    }

    const int totalBlocks = static_cast<int>(enemy.bodyBlocks.size());
    const float hpRatio = std::clamp(static_cast<float>(std::max(0, enemy.currentHp)) / static_cast<float>(enemy.maxHp), 0.0f, 1.0f);
    const int targetLost = enemy.currentHp <= 0 ? totalBlocks :
        std::clamp(static_cast<int>(std::floor((1.0f - hpRatio) * static_cast<float>(totalBlocks))), 0, totalBlocks);
    int dropsRemaining = std::max(0, targetLost - enemy.bodyBlocksLost);
    if (dropsRemaining <= 0)
    {
        return;
    }

    while (dropsRemaining > 0)
    {
        std::vector<size_t> visibleIndices;
        visibleIndices.reserve(enemy.bodyBlocks.size());
        for (size_t index = 0; index < enemy.bodyBlocks.size(); ++index)
        {
            if (enemy.bodyBlocks[index].visible)
            {
                visibleIndices.push_back(index);
            }
        }
        if (visibleIndices.empty())
        {
            break;
        }

        std::uniform_int_distribution<size_t> indexDist(0, visibleIndices.size() - 1);
        Brotato3D::FEnemyBodyBlockRuntime& block = enemy.bodyBlocks[visibleIndices[indexDist(rng_)]];
        block.visible = false;
        NodeUtils::SetVisible(block.node, false);
        ++enemy.bodyBlocksLost;
        --dropsRemaining;
    }
}

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
    const glm::vec3 spawnPos(worldPos.x, def.size.y * 0.5f + SampleArenaGroundY(worldPos), worldPos.z);
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
        const uint32_t runtimeTag = static_cast<uint32_t>(std::distance(enemies_.begin(), reusableEnemy)) + 1U;
        enemy.node = reusableEnemy->node;
        enemy.bodyBlocks = std::move(reusableEnemy->bodyBlocks);
        enemy.kinematicBodyId = reusableEnemy->kinematicBodyId.IsInvalid() ? AcquireEnemyKinematicBody(enemyId) :
                                                                        reusableEnemy->kinematicBodyId;
        enemy.runtimeTag = runtimeTag;
        *reusableEnemy = std::move(enemy);
        if (reusableEnemy->bodyBlocks.empty())
        {
            CreateEnemyBodyBlocks(*reusableEnemy, visual, enemyId);
        }
        ResetEnemyBodyBlocks(*reusableEnemy);
        reusableEnemy->node->SetTranslation(reusableEnemy->worldPos);
        reusableEnemy->node->SetScale(glm::vec3(1.0f));
        // Spawn activation is a positional snap; use a stable fallback step for the kinematic body.
        SyncEnemyKinematicBody(*reusableEnemy, 1.0 / 60.0);
        return;
    }

    enemy.kinematicBodyId = AcquireEnemyKinematicBody(enemyId);
    enemy.node = SceneBuilder::CreateRenderNode(fmt::format("Brotato3D_Enemy_{}_{}", enemyId, enemies_.size()), spawnPos, glm::vec3(1.0f),
                                  GetEngine().GetScene().GenerateInstanceId(), visual.modelId, visual.materialId, false);
    GetEngine().GetScene().AddNode(enemy.node);
    enemies_.push_back(enemy);
    enemies_.back().runtimeTag = static_cast<uint32_t>(enemies_.size());
    CreateEnemyBodyBlocks(enemies_.back(), visual, enemyId);
    ResetEnemyBodyBlocks(enemies_.back());
    GetEngine().GetScene().MarkTransformDirty();
    // Spawn activation is a positional snap; use a stable fallback step for the kinematic body.
    SyncEnemyKinematicBody(enemies_.back(), 1.0 / 60.0);
}

void Brotato3DGameInstance::UpdateEnemies(double deltaSeconds)
{
    const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
    for (auto& enemy : enemies_)
    {
        if (!enemy.alive)
        {
            continue;
        }

        enemy.contactCooldownMs = std::max(0.0f, enemy.contactCooldownMs - deltaMs);
        enemy.rangedFireCooldownMs = std::max(0.0f, enemy.rangedFireCooldownMs - deltaMs);
        enemy.chargeCooldownMs = std::max(0.0f, enemy.chargeCooldownMs - deltaMs);
        enemy.healIntervalMs = std::max(0.0f, enemy.healIntervalMs - deltaMs);
        enemy.hitFlashRemainingMs = std::max(0.0f, enemy.hitFlashRemainingMs - deltaMs);
        enemy.mortarFireCooldownMs = std::max(0.0f, enemy.mortarFireCooldownMs - deltaMs);
        enemy.lanceCooldownMs = std::max(0.0f, enemy.lanceCooldownMs - deltaMs);

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
        else if ((enemy.def->mortar.enabled && enemy.mortarTelegraphRemainingMs > 0.0f) ||
                 (enemy.def->lance.enabled &&
                  (enemy.lanceState == Brotato3D::ELanceState::Telegraph ||
                   enemy.lanceState == Brotato3D::ELanceState::Dashing)))
        {
            activeMaterial = enemy.lanceState == Brotato3D::ELanceState::Dashing ? enemy.hitFlashMaterialId :
                                                                                   enemy.warningMaterialId;
        }
        else if ((enemy.def->charge.enabled && enemy.charging) ||
                 (enemy.def->bomb.enabled && enemy.bombFuseMs >= 0.0f))
        {
            activeMaterial = enemy.warningMaterialId;
        }
        SetEnemyVisualMaterial(enemy, activeMaterial);

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
                enemy.alive = false;
                enemy.fading = false;
                DeactivateEnemyKinematicBody(enemy);
                SetEnemyVisualVisible(enemy, false);
                continue;
            }
            SyncEnemyKinematicBody(enemy, deltaSeconds);
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

        bool customEnemyHandled = false;
        if (playerDistance > 0.001f)
        {
            const glm::vec3 toPlayerDir = toPlayer / playerDistance;
            if (enemy.def->mortar.enabled)
            {
                customEnemyHandled = UpdateMortarTank(enemy, deltaSeconds, deltaMs, toPlayerDir, playerDistance);
            }
            else if (enemy.def->lance.enabled)
            {
                customEnemyHandled = UpdateLanceCharger(enemy, deltaSeconds, deltaMs, toPlayerDir, playerDistance);
            }

            if (!customEnemyHandled)
            {
                glm::vec3 moveDir = toPlayerDir;
                float moveSpeed = enemy.def->moveSpeed;
                if (enemy.bossPhase2Active)
                {
                    moveSpeed *= enemy.def->boss.phase2MoveSpeedMult;
                }
                if (enemy.def->bomb.enabled && playerDistance <= enemy.def->bomb.triggerDistance)
                {
                    enemy.bombFuseMs = enemy.def->bomb.fuseMs;
                    SyncEnemyKinematicBody(enemy, deltaSeconds);
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

                    if (enemy.rangedFireCooldownMs <= 0.0f &&
                        !IsSegmentBlockedByExtractionVehicle(enemy.worldPos, player_.worldPos, enemy.def->ranged.size))
                    {
                        SpawnEnemyProjectile(enemy, toPlayerDir);
                        enemy.rangedFireCooldownMs = enemy.def->ranged.intervalMs;
                    }
                }

                if (moveSpeed > 0.0f)
                {
                    enemy.worldPos += moveDir * moveSpeed * static_cast<float>(deltaSeconds);
                    enemy.worldPos = ClampToArena(enemy.worldPos, enemy.radius, arenaHalfExtent_);
                    enemy.worldPos = ResolveExtractionVehicleCollision(enemy.worldPos, enemy.radius);
                    enemy.worldPos = ClampToArena(enemy.worldPos, enemy.radius, arenaHalfExtent_);
                    enemy.worldPos.y = enemy.def->size.y * 0.5f + SampleArenaGroundY(enemy.worldPos);
                    enemy.node->SetTranslation(enemy.worldPos);
                }
            }
        }

        if (DistanceXZ(enemy.worldPos, player_.worldPos) < enemy.radius + player_.radius && enemy.contactCooldownMs <= 0.0f)
        {
            int contactDamage = enemy.def->contactDamage;
            if (enemy.def->lance.enabled && enemy.lanceState == Brotato3D::ELanceState::Dashing)
            {
                contactDamage = static_cast<int>(std::round(contactDamage * enemy.def->lance.dashContactDamageMult));
                enemy.lanceState = Brotato3D::ELanceState::Recovering;
                enemy.lanceStateMs = enemy.def->lance.recoverMs;
                enemy.lanceDashRemainingDist = 0.0f;
                StartScreenShake(180.0f, 3.0f);
            }
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
                SetWorldPhysicsPaused(true);
            }
            DamagePlayer(contactDamage, 150.0f, 180.0f);
        }
        SyncEnemyKinematicBody(enemy, deltaSeconds);
    }
}

bool Brotato3DGameInstance::UpdateMortarTank(Brotato3D::FEnemyRuntime& enemy,
                                             double deltaSeconds,
                                             float deltaMs,
                                             const glm::vec3& toPlayerDir,
                                             float playerDistance)
{
    if (!enemy.def || !enemy.def->mortar.enabled)
    {
        return false;
    }

    const Brotato3D::FMortarDef& mortar = enemy.def->mortar;
    if (enemy.mortarTelegraphRemainingMs > 0.0f)
    {
        enemy.mortarTelegraphRemainingMs -= deltaMs;
        if (enemy.mortarTelegraphRemainingMs <= 0.0f)
        {
            CancelGroundIndicatorsForEnemy(enemy.runtimeTag);
            PushExplosionRing(enemy.mortarTargetPos, glm::vec4(1.0f, 0.30f, 0.10f, 1.0f), mortar.explosionRadius);
            SpawnTempLight(enemy.mortarTargetPos, glm::vec3(1.0f, 0.45f, 0.10f), 5.0f, 250.0f);
            StartScreenShake(180.0f, 3.0f);
            if (DistanceXZ(enemy.mortarTargetPos, player_.worldPos) <= mortar.explosionRadius)
            {
                DamagePlayer(static_cast<int>(std::round(static_cast<float>(mortar.explosionDamage) * Brotato3D::MasterDifficulty)),
                             180.0f,
                             250.0f);
            }
            enemy.mortarFireCooldownMs = mortar.fireIntervalMs;
            enemy.mortarTelegraphRemainingMs = 0.0f;
        }
        SyncEnemyKinematicBody(enemy, deltaSeconds);
        return true;
    }

    if (enemy.mortarFireCooldownMs <= 0.0f &&
        playerDistance >= mortar.throwRangeMin &&
        playerDistance <= mortar.throwRangeMax)
    {
        glm::vec3 target = player_.worldPos + playerVelocity_ * mortar.leadFactor;
        target = ClampToArena(target, 0.0f, arenaHalfExtent_);
        target.y = 0.05f;
        enemy.mortarTargetPos = target;
        enemy.mortarTelegraphRemainingMs = std::max(1.0f, mortar.telegraphMs);

        Brotato3D::FGroundIndicator indicator{};
        indicator.shape = Brotato3D::EGroundIndicatorShape::Circle;
        indicator.worldPos = target;
        indicator.radius = mortar.explosionRadius;
        indicator.color = glm::vec4(1.0f, 0.20f, 0.08f, 0.9f);
        indicator.totalMs = enemy.mortarTelegraphRemainingMs;
        indicator.remainingMs = enemy.mortarTelegraphRemainingMs;
        indicator.enemyTag = enemy.runtimeTag;
        PushGroundIndicator(indicator);
        SyncEnemyKinematicBody(enemy, deltaSeconds);
        return true;
    }

    glm::vec3 moveDir = toPlayerDir;
    float moveSpeed = 0.0f;
    if (playerDistance < mortar.throwRangeMin)
    {
        moveDir = -toPlayerDir;
        moveSpeed = enemy.def->moveSpeed;
    }
    else if (playerDistance > mortar.throwRangeMax)
    {
        moveSpeed = enemy.def->moveSpeed;
    }

    if (moveSpeed > 0.0f)
    {
        enemy.worldPos += moveDir * moveSpeed * static_cast<float>(deltaSeconds);
        enemy.worldPos = ClampToArena(enemy.worldPos, enemy.radius, arenaHalfExtent_);
        enemy.worldPos = ResolveExtractionVehicleCollision(enemy.worldPos, enemy.radius);
        enemy.worldPos = ClampToArena(enemy.worldPos, enemy.radius, arenaHalfExtent_);
        enemy.worldPos.y = enemy.def->size.y * 0.5f + SampleArenaGroundY(enemy.worldPos);
        enemy.node->SetTranslation(enemy.worldPos);
    }
    return true;
}

bool Brotato3DGameInstance::UpdateLanceCharger(Brotato3D::FEnemyRuntime& enemy,
                                               double deltaSeconds,
                                               float deltaMs,
                                               const glm::vec3& toPlayerDir,
                                               float playerDistance)
{
    if (!enemy.def || !enemy.def->lance.enabled)
    {
        return false;
    }

    const Brotato3D::FLanceDef& lance = enemy.def->lance;
    switch (enemy.lanceState)
    {
    case Brotato3D::ELanceState::Idle:
        if (enemy.lanceCooldownMs <= 0.0f && playerDistance <= lance.windupRangeMin)
        {
            enemy.lanceState = Brotato3D::ELanceState::Telegraph;
            enemy.lanceStateMs = std::max(1.0f, lance.telegraphMs);
            enemy.lanceDashDir = toPlayerDir;
            enemy.lanceDashRemainingDist = lance.dashDistanceMax;

            Brotato3D::FGroundIndicator indicator{};
            indicator.shape = Brotato3D::EGroundIndicatorShape::Strip;
            indicator.worldPos = enemy.worldPos + glm::vec3(0.0f, 0.05f, 0.0f);
            indicator.endPos = ClampToArena(enemy.worldPos + enemy.lanceDashDir * lance.dashDistanceMax,
                                            enemy.radius,
                                            arenaHalfExtent_);
            indicator.endPos = ResolveExtractionVehicleCollision(indicator.endPos, enemy.radius);
            indicator.endPos = ClampToArena(indicator.endPos, enemy.radius, arenaHalfExtent_);
            indicator.endPos.y = 0.05f;
            indicator.width = 0.6f;
            indicator.color = glm::vec4(1.0f, 0.18f, 0.08f, 0.85f);
            indicator.totalMs = enemy.lanceStateMs;
            indicator.remainingMs = enemy.lanceStateMs;
            indicator.enemyTag = enemy.runtimeTag;
            PushGroundIndicator(indicator);
            return true;
        }
        enemy.worldPos += toPlayerDir * enemy.def->moveSpeed * static_cast<float>(deltaSeconds);
        enemy.worldPos = ClampToArena(enemy.worldPos, enemy.radius, arenaHalfExtent_);
        enemy.worldPos = ResolveExtractionVehicleCollision(enemy.worldPos, enemy.radius);
        enemy.worldPos = ClampToArena(enemy.worldPos, enemy.radius, arenaHalfExtent_);
        enemy.worldPos.y = enemy.def->size.y * 0.5f + SampleArenaGroundY(enemy.worldPos);
        enemy.node->SetTranslation(enemy.worldPos);
        return true;

    case Brotato3D::ELanceState::Telegraph:
        enemy.lanceStateMs -= deltaMs;
        if (enemy.lanceStateMs <= 0.0f)
        {
            CancelGroundIndicatorsForEnemy(enemy.runtimeTag);
            enemy.lanceState = Brotato3D::ELanceState::Dashing;
            enemy.lanceDashStartPos = enemy.worldPos;
            enemy.lanceDashRemainingDist = lance.dashDistanceMax;
            Brotato3D::PlayWeaponFireSfx("shotgun");
        }
        return true;

    case Brotato3D::ELanceState::Dashing:
    {
        const float dashStep = std::min(lance.dashSpeed * static_cast<float>(deltaSeconds), enemy.lanceDashRemainingDist);
        const glm::vec3 before = enemy.worldPos;
        const glm::vec3 intendedPos = enemy.worldPos + enemy.lanceDashDir * dashStep;
        glm::vec3 nextPos = intendedPos;
        nextPos = ClampToArena(nextPos, enemy.radius, arenaHalfExtent_);
        nextPos = ResolveExtractionVehicleCollision(nextPos, enemy.radius);
        nextPos = ClampToArena(nextPos, enemy.radius, arenaHalfExtent_);
        nextPos.y = enemy.def->size.y * 0.5f + SampleArenaGroundY(nextPos);
        enemy.worldPos = nextPos;
        enemy.node->SetTranslation(enemy.worldPos);
        enemy.lanceDashRemainingDist -= glm::length(glm::vec2(enemy.worldPos.x - before.x, enemy.worldPos.z - before.z));
        const bool hitWall = DistanceXZ(intendedPos, enemy.worldPos) > 0.001f;
        if (enemy.lanceDashRemainingDist <= 0.001f || hitWall)
        {
            enemy.lanceState = Brotato3D::ELanceState::Recovering;
            enemy.lanceStateMs = lance.recoverMs;
            enemy.lanceDashRemainingDist = 0.0f;
        }
        return true;
    }

    case Brotato3D::ELanceState::Recovering:
        enemy.lanceStateMs -= deltaMs;
        if (enemy.lanceStateMs <= 0.0f)
        {
            enemy.lanceState = Brotato3D::ELanceState::Idle;
            enemy.lanceCooldownMs = lance.cooldownMs;
            enemy.lanceStateMs = 0.0f;
        }
        return true;
    }

    return true;
}

void Brotato3DGameInstance::KillEnemy(Brotato3D::FEnemyRuntime& enemy, bool dropLoot)
{
    if (!enemy.alive)
    {
        return;
    }
    CancelGroundIndicatorsForEnemy(enemy.runtimeTag);
    enemy.mortarTelegraphRemainingMs = 0.0f;
    enemy.lanceState = Brotato3D::ELanceState::Idle;
    enemy.alive = false;
    DeactivateEnemyKinematicBody(enemy);
    enemy.fading = false;
    enemy.deathFadeMs = 0.0f;
    enemy.node->SetScale(glm::vec3(1.0f));
    SetEnemyVisualVisible(enemy, true);
    if (dropLoot)
    {
        ++killCount_;
        Brotato3D::PlayEnemyDeathSfx(enemy.def ? enemy.def->name : std::string{});
        if (!enemy.def || !enemy.def->boss.enabled)
        {
            SpawnKillMaterialDebris(enemy);
        }
        ProcessOnKillTriggers(enemy.worldPos);
    }
    if (enemy.def && enemy.def->boss.enabled)
    {
        bossVictoryDelayMs_ = std::max(bossVictoryDelayMs_, 1200.0f);
        spdlog::info("[Brotato3D] [boss defeated]");
    }
    if (dropLoot && enemy.def && enemy.def->boss.enabled)
    {
        const glm::vec3 deathSprayDir = ResolveEnemyDebrisDir(enemy, enemy.worldPos - player_.worldPos);
        std::uniform_real_distribution<float> unitDist(-1.0f, 1.0f);
        std::uniform_real_distribution<float> chunkSpeedDist(5.0f, 9.0f);
        std::uniform_real_distribution<float> bossSpeedDist(6.0f, 10.0f);
        for (int index = 0; index < 50; ++index)
        {
            glm::vec3 dir = deathSprayDir * 2.2f +
                            glm::vec3(unitDist(rng_) * 0.45f,
                                      std::uniform_real_distribution<float>(0.35f, 0.95f)(rng_),
                                      unitDist(rng_) * 0.45f);
            if (glm::length(dir) < 0.001f)
            {
                dir = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            dir = glm::normalize(dir);
            const uint32_t matId = (index % 4 == 0) ? enemy.darkMaterialId : enemy.materialId;
            SpawnDebris(Brotato3D::EDebrisKind::Chunk, enemy.worldPos + dir * 0.2f, dir, chunkSpeedDist(rng_), matId, 1, 0.0f);
        }
        for (int index = 0; index < 8; ++index)
        {
            glm::vec3 dir = deathSprayDir * 2.5f +
                            glm::vec3(unitDist(rng_) * 0.4f,
                                      std::uniform_real_distribution<float>(0.4f, 1.0f)(rng_),
                                      unitDist(rng_) * 0.4f);
            if (glm::length(dir) < 0.001f)
            {
                dir = glm::vec3(0.0f, 1.0f, 0.0f);
            }
            dir = glm::normalize(dir);
            SpawnDebris(Brotato3D::EDebrisKind::BossChunk, enemy.worldPos + dir * 0.4f, dir, bossSpeedDist(rng_),
                        enemy.materialId, 1, 0.0f);
        }
        SpawnKillMaterialDebris(enemy, 3, 1.0f, 12);
        StartScreenShake(800.0f, 5.0f);
        explosionRings_.push_back({enemy.worldPos, glm::vec4(1.0f, 0.72f, 0.18f, 1.0f), 800.0f, 800.0f, 4.0f});
        explosionRings_.push_back({enemy.worldPos, glm::vec4(1.0f, 0.92f, 0.40f, 1.0f), 1200.0f, 1200.0f, 8.0f});
        SpawnTempLight(enemy.worldPos, glm::vec3(1.0f, 0.85f, 0.40f), 12.0f, 800.0f);
        bossKillFlashMs_ = 100.0f;
        timeScaleRecoveryMs_ = 1200.0f;
    }
    SetEnemyVisualVisible(enemy, false);
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

void Brotato3DGameInstance::SpawnKillMaterialDebris(const Brotato3D::FEnemyRuntime& enemy,
                                                    int countMultiplier,
                                                    float spawnRadius,
                                                    int minCount)
{
    if (!enemy.def)
    {
        return;
    }

    const int count = std::max(minCount, enemy.def->materialDrop * std::max(1, countMultiplier));
    if (count <= 0)
    {
        return;
    }

    const glm::vec3 deathSprayDir = ResolveEnemyDebrisDir(enemy, enemy.worldPos - player_.worldPos);
    std::uniform_real_distribution<float> speedDist(4.5f, 7.0f);
    for (int index = 0; index < count; ++index)
    {
        const float angle = static_cast<float>(index) / static_cast<float>(count) * glm::two_pi<float>();
        const glm::vec3 radial(std::cos(angle), 0.0f, std::sin(angle));
        const glm::vec3 spawnPos = enemy.worldPos + radial * spawnRadius + glm::vec3(0.0f, enemy.def->size.y * 0.45f, 0.0f);
        glm::vec3 dir = deathSprayDir * 1.8f + radial * 0.45f + glm::vec3(0.0f, 0.6f, 0.0f);
        if (glm::length(dir) < 0.001f)
        {
            dir = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        dir = glm::normalize(dir);
        SpawnDebris(Brotato3D::EDebrisKind::Chunk,
                    spawnPos,
                    dir,
                    speedDist(rng_),
                    materialDebrisMatId_,
                    1,
                    0.0f,
                    Brotato3D::EDebrisPayload::Material,
                    1);
    }
}


