#include "KongLie3DBattleSystem.hpp"

#include "Assets/Core/Node.h"
#include "Assets/Data/Material.hpp"
#include "Assets/Loaders/FProcModel.h"
#include "KongLie3DAudio.hpp"
#include "KongLie3DSkills.hpp"
#include "Runtime/Scene/NodeUtils.h"
#include "Runtime/Scene/SceneBuilder.h"
#include "Runtime/Subsystems/NextPhysics.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Engine.hpp"

#include <spdlog/spdlog.h>

namespace
{
    constexpr float TickMs = 100.0f;
    constexpr float AttackTraceLifetimeMs = 180.0f;
    constexpr float DamagePopupLifetimeMs = 600.0f;
    constexpr float DeathAnimationDurationMs = 500.0f;
    constexpr float DeathSinkDistance = 0.3f;
    constexpr float BattleLimitMs = 30000.0f;
    constexpr float OvertimeDrawMs = 45000.0f;
    constexpr int BenchRow = 8;
    constexpr float BenchWorldZ = 8.5f;
    constexpr float MinMoveSpeed = 0.01f;
    constexpr float MinAttackSpeed = 0.01f;
    constexpr float HitFlashDurationMs = 80.0f;
    constexpr float MaxScreenShakeMs = 200.0f;
    constexpr float ScreenShakePerHitMs = 50.0f;
    constexpr float DebrisDurationMs = 320.0f;
    constexpr int DebrisPerHit = 3;
    constexpr int DebrisPerDeath = 6;
    constexpr float MaxProjectileDurationMs = 400.0f;
    constexpr float UltimateFlashDurationMs = 300.0f;
    constexpr float UltimateCameraDurationMs = 600.0f;
    constexpr float UltimateTitleDurationMs = 1000.0f;
    constexpr float UltimateLightDurationMs = 600.0f;
    constexpr float KnockoutProxyDurationMs = 1500.0f;
    const glm::vec3 HiddenProxyPosition(0.0f, -20.0f, 0.0f);

    bool IsCombatPiece(const KongLie3D::FPieceRuntime& piece)
    {
        return piece.alive && !piece.onBench;
    }

    int ChebyshevDistance(const KongLie3D::FPieceRuntime& lhs, const KongLie3D::FPieceRuntime& rhs)
    {
        return std::max(std::abs(lhs.col - rhs.col), std::abs(lhs.row - rhs.row));
    }

    int StepTowards(int current, int target)
    {
        if (current < target)
        {
            return current + 1;
        }
        if (current > target)
        {
            return current - 1;
        }
        return current;
    }

    glm::vec3 ComputeWorldPosition(const KongLie3D::FPieceRuntime& piece)
    {
        return glm::vec3(
            static_cast<float>(piece.col),
            piece.dimensions.y * 0.5f * piece.visualScale,
            piece.onBench || piece.row == BenchRow ? BenchWorldZ : static_cast<float>(piece.row));
    }

    float ComputeAttackCooldownMs(float atkSpeed)
    {
        const float clampedAttackSpeed = std::max(atkSpeed, MinAttackSpeed);
        const float baseCooldownMs = clampedAttackSpeed >= 1.0f ? (1000.0f / clampedAttackSpeed)
                                                                : (clampedAttackSpeed * 1000.0f);
        return baseCooldownMs + TickMs;
    }

    void RestorePieceMaterials(KongLie3D::FPieceRuntime& piece)
    {
        NodeUtils::SetPrimaryMaterial(piece.node, piece.materialId);
        for (const auto& [node, materialId] : piece.visualAttachments)
        {
            NodeUtils::SetPrimaryMaterial(node, materialId);
        }
    }

    glm::vec3 ComputeImpactDebrisVelocity(size_t index, float speedScale)
    {
        static constexpr std::array<glm::vec3, 8> Directions = {
            glm::vec3(-0.28f, 0.22f, 0.16f),
            glm::vec3(0.00f, 0.26f, -0.14f),
            glm::vec3(0.24f, 0.21f, 0.18f),
            glm::vec3(-0.18f, 0.24f, -0.26f),
            glm::vec3(0.12f, 0.23f, 0.28f),
            glm::vec3(-0.30f, 0.20f, -0.06f),
            glm::vec3(0.32f, 0.21f, -0.14f),
            glm::vec3(0.06f, 0.27f, 0.00f),
        };
        return Directions[index % Directions.size()] * speedScale;
    }

    bool PieceHasSynergy(const KongLie3D::FPieceRuntime& piece, const std::string& synergyId)
    {
        return std::find(piece.baseDef.synergies.begin(), piece.baseDef.synergies.end(), synergyId) != piece.baseDef.synergies.end();
    }
}

namespace KongLie3D
{
    void FBattleSystem::BindPieces(std::vector<FPieceRuntime>* pieces)
    {
        pieces_ = pieces;
        blueUltimateLightMaterialId_ = std::numeric_limits<uint32_t>::max();
        orangeUltimateLightMaterialId_ = std::numeric_limits<uint32_t>::max();
        Reset();
    }

    void FBattleSystem::ConfigureVisualResources(uint32_t hitFlashMaterialId,
                                                 std::vector<FProjectilePoolEntry> projectilePool,
                                                 std::vector<FImpactDebrisPoolEntry> debrisPool)
    {
        hitFlashMaterialId_ = hitFlashMaterialId;
        projectilePool_ = std::move(projectilePool);
        debrisPool_ = std::move(debrisPool);
        projectiles_.clear();
        impactDebris_.clear();
    }

    void FBattleSystem::SetRelics(std::vector<FRelicDef> relics)
    {
        relics_ = std::move(relics);
        if (selectedRelicIndex_ < 0 || selectedRelicIndex_ >= static_cast<int>(relics_.size()))
        {
            selectedRelicIndex_ = -1;
        }
    }

    void FBattleSystem::SetSynergies(std::vector<FSynergyDef> synergies)
    {
        synergyDefs_ = std::move(synergies);
        activeSynergies_.clear();
    }

    void FBattleSystem::SetEnemyDamageMultiplier(float enemyDamageMultiplier)
    {
        enemyDamageMultiplier_ = std::max(0.1f, enemyDamageMultiplier);
    }

    void FBattleSystem::Reset()
    {
        if (NextEngine* engine = NextEngine::GetInstance(); engine && !tempLights_.empty())
        {
            auto& lights = engine->GetScene().Lights();
            std::vector<int> lightIndices;
            lightIndices.reserve(tempLights_.size());
            for (const FTempLight& tempLight : tempLights_)
            {
                if (tempLight.lightIndex >= 0 && tempLight.lightIndex < static_cast<int>(lights.size()))
                {
                    lightIndices.push_back(tempLight.lightIndex);
                }
            }

            std::sort(lightIndices.begin(), lightIndices.end(), std::greater<>());
            lightIndices.erase(std::unique(lightIndices.begin(), lightIndices.end()), lightIndices.end());
            for (const int lightIndex : lightIndices)
            {
                lights.erase(lights.begin() + lightIndex);
            }
            sceneDirty_ = sceneDirty_ || !lightIndices.empty();
        }

        tempLights_.clear();
        activeSynergies_.clear();
        state_ = EBattleState::Deployment;
        paused_ = false;
        tickAccumulatorMs_ = 0.0f;
        elapsedMs_ = 0.0f;
        dmgMult_ = 1.0f;
        healMult_ = 1.0f;
        overtimeActive_ = false;
        overtimeStartMs_ = 0.0f;
        winnerTeam_.clear();
        attackTraces_.clear();
        skillEffects_.clear();
        damagePopups_.clear();
        pendingUltimateIds_.clear();
        screenShakeMs_ = 0.0f;
        projectiles_.clear();
        impactDebris_.clear();
        ultimatePresentation_ = {};
        enemyDamageMultiplier_ = 1.0f;
        sceneDirty_ = true;

        for (auto& pooledProjectile : projectilePool_)
        {
            pooledProjectile.active = false;
            NodeUtils::SetVisibleRecursive(pooledProjectile.node, false);
        }
        for (auto& pooledDebris : debrisPool_)
        {
            pooledDebris.active = false;
            NodeUtils::SetVisibleRecursive(pooledDebris.node, false);
        }

        if (!pieces_)
        {
            return;
        }

        for (auto& piece : *pieces_)
        {
            piece.def = piece.baseDef;
            piece.currentHp = piece.def.hp;
            piece.currentMana = 0;
            piece.col = piece.initialCol;
            piece.row = piece.initialRow;
            piece.alive = true;
            piece.onBench = piece.initialOnBench;
            piece.attackCooldownMs = 0.0f;
            piece.healCooldownMs = 0.0f;
            piece.moveCooldownMs = 0.0f;
            piece.moveDurationMs = 0.0f;
            piece.moveElapsedMs = 0.0f;
            piece.wCooldownMs = 0.0f;
            piece.ultimateCooldownMs = 0.0f;
            piece.ultimateUsed = false;
            piece.shield = 0;
            piece.shieldTimerMs = 0.0f;
            piece.stunTimerMs = 0.0f;
            piece.furyTimerMs = 0.0f;
            piece.hitFlashMs = 0.0f;
            piece.hitFlashActive = false;
            piece.deathAnimationMs = 0.0f;
            piece.knockoutTimerMs = 0.0f;
            piece.statDmgAD = 0;
            piece.statDmgAP = 0;
            piece.statDmgTaken = 0;
            piece.statHeal = 0;
            piece.prevWorldPos = ComputeWorldPosition(piece);
            piece.targetWorldPos = piece.prevWorldPos;
            piece.lastAttackerPos = piece.targetWorldPos;
            if (piece.node)
            {
                RestorePieceMaterials(piece);
                piece.node->SetTranslation(piece.targetWorldPos);
                piece.node->RecalcTransform(true);
            }
            NodeUtils::SetVisibleRecursive(piece.node, true);

            if (piece.knockoutNode)
            {
                piece.knockoutNode->SetTranslation(HiddenProxyPosition);
                piece.knockoutNode->SetRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                piece.knockoutNode->SetScale(piece.dimensions * piece.visualScale);
                piece.knockoutNode->RecalcTransform(true);
                NodeUtils::SetVisibleRecursive(piece.knockoutNode, false);
            }
            if (NextPhysics* physics = NextEngine::GetInstance()->GetPhysicsEngine();
                physics && piece.knockoutNode && !piece.knockoutBodyId.IsInvalid())
            {
                physics->SetBodyTransform(piece.knockoutBodyId, HiddenProxyPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
                physics->SetBodyVelocity(piece.knockoutBodyId, glm::vec3(0.0f), glm::vec3(0.0f));
                physics->SetBodyActive(piece.knockoutBodyId, false);
            }
        }
    }

    void FBattleSystem::Start()
    {
        if (!pieces_ || state_ != EBattleState::Deployment)
        {
            return;
        }

        ApplySelectedRelic();
        activeSynergies_ = EvaluateSynergiesFromDeployment();
        ApplySynergyBonuses(activeSynergies_);
        PlayBattleStartSfx();

        state_ = EBattleState::Battle;
        paused_ = false;
        tickAccumulatorMs_ = 0.0f;
        elapsedMs_ = 0.0f;
        dmgMult_ = 1.0f;
        healMult_ = 1.0f;
        overtimeActive_ = false;
        overtimeStartMs_ = 0.0f;
        winnerTeam_.clear();
        attackTraces_.clear();
        skillEffects_.clear();
        damagePopups_.clear();
        pendingUltimateIds_.clear();
        if (const FRelicDef* relic = GetSelectedRelic())
        {
            spdlog::info("[KongLie3D] Battle started with relic '{}'", relic->name);
        }
        else
        {
            spdlog::info("[KongLie3D] Battle started");
        }
    }

    void FBattleSystem::Update(double deltaSeconds)
    {
        if (state_ == EBattleState::Battle && paused_)
        {
            return;
        }

        const float rawDeltaMs = static_cast<float>(deltaSeconds * 1000.0);
        const float battleDeltaMs = rawDeltaMs * GetSpeedMultiplier();
        UpdateAttackTraces(battleDeltaMs);
        UpdateSkillEffects(battleDeltaMs);
        UpdateDamagePopups(battleDeltaMs);
        UpdateProjectiles(battleDeltaMs);
        UpdateImpactDebris(battleDeltaMs);
        UpdateHitFlashes(battleDeltaMs);
        UpdateUltimatePresentation(rawDeltaMs);
        UpdateTempLights(rawDeltaMs);
        screenShakeMs_ = std::max(0.0f, screenShakeMs_ - battleDeltaMs);

        if (state_ == EBattleState::Battle)
        {
            elapsedMs_ += battleDeltaMs;
            tickAccumulatorMs_ += battleDeltaMs;

            while (tickAccumulatorMs_ >= TickMs)
            {
                Tick();
                tickAccumulatorMs_ -= TickMs;
                if (state_ != EBattleState::Battle)
                {
                    tickAccumulatorMs_ = 0.0f;
                    break;
                }
            }
        }

        UpdateMovementInterpolation(battleDeltaMs);
        UpdateDeathAnimations(battleDeltaMs);
    }

    bool FBattleSystem::ConsumeSceneDirty()
    {
        const bool wasDirty = sceneDirty_;
        sceneDirty_ = false;
        return wasDirty;
    }

    void FBattleSystem::TogglePause()
    {
        if (state_ != EBattleState::Battle)
        {
            return;
        }

        paused_ = !paused_;
        spdlog::info("[KongLie3D] Battle {}", paused_ ? "paused" : "resumed");
    }

    void FBattleSystem::SetSpeedMultiplier(float speedMultiplier)
    {
        speedMultiplier_ = std::clamp(speedMultiplier, 1.0f, 4.0f);
        spdlog::info("[KongLie3D] Battle speed set to {}x", speedMultiplier_);
    }

    void FBattleSystem::CycleSpeedMultiplier()
    {
        const float current = GetSpeedMultiplier();
        if (current < 1.5f)
        {
            SetSpeedMultiplier(2.0f);
        }
        else if (current < 3.0f)
        {
            SetSpeedMultiplier(4.0f);
        }
        else
        {
            SetSpeedMultiplier(1.0f);
        }
    }

    void FBattleSystem::RequestUltimate(const std::string& heroId)
    {
        FPieceRuntime* piece = FindPieceById(heroId);
        if (!piece)
        {
            spdlog::warn("[KongLie3D] Ultimate request ignored: unknown hero '{}'", heroId);
            return;
        }

        if (!IsUltimateReady(*piece))
        {
            spdlog::info("[KongLie3D] Ultimate request ignored: '{}' not ready", heroId);
            return;
        }

        if (std::find(pendingUltimateIds_.begin(), pendingUltimateIds_.end(), heroId) == pendingUltimateIds_.end())
        {
            pendingUltimateIds_.push_back(heroId);
        }
        spdlog::info("[KongLie3D] Ultimate requested for '{}' ({})", heroId, piece->def.skillUltimate);
    }

    void FBattleSystem::SelectRelic(const std::string& relicId)
    {
        if (state_ != EBattleState::Deployment)
        {
            return;
        }

        for (size_t index = 0; index < relics_.size(); ++index)
        {
            if (relics_[index].id == relicId)
            {
                selectedRelicIndex_ = static_cast<int>(index);
                spdlog::info("[KongLie3D] Selected relic '{}'", relics_[index].name);
                return;
            }
        }

        spdlog::warn("[KongLie3D] Ignored unknown relic '{}'", relicId);
    }

    void FBattleSystem::TriggerUltimatePresentation(const FPieceRuntime& caster, std::string title, const glm::vec4& color)
    {
        ultimatePresentation_.cameraFocusPos = caster.node ? caster.node->WorldTranslation() : caster.targetWorldPos;
        ultimatePresentation_.flashColor = color;
        ultimatePresentation_.titleColor = color;
        ultimatePresentation_.title = std::move(title);
        ultimatePresentation_.flashDurationMs = UltimateFlashDurationMs;
        ultimatePresentation_.flashRemainingMs = UltimateFlashDurationMs;
        ultimatePresentation_.cameraFocusDurationMs = UltimateCameraDurationMs;
        ultimatePresentation_.cameraFocusRemainingMs = UltimateCameraDurationMs;
        ultimatePresentation_.titleDurationMs = UltimateTitleDurationMs;
        ultimatePresentation_.titleRemainingMs = UltimateTitleDurationMs;
    }

    void FBattleSystem::TriggerUltimateAreaLight(const FPieceRuntime& caster, const glm::vec3& color)
    {
        if (!caster.node)
        {
            return;
        }

        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            return;
        }

        auto& scene = engine->GetScene();
        auto& lights = scene.Lights();
        const uint32_t materialId = EnsureTempLightMaterial(color);

        std::vector<Assets::LightObject> createdLights;
        const glm::vec3 center = caster.node->WorldTranslation() + glm::vec3(0.0f, 1.25f, 0.0f);
        const glm::vec3 right(4.6f, 0.0f, 0.0f);
        const glm::vec3 up(0.0f, 0.0f, 4.6f);
        Assets::FProcModel::CreateAreaLight("KongLie3D_UltimateLight",
                                            center - right * 0.5f - up * 0.5f,
                                            right,
                                            up,
                                            materialId,
                                            createdLights);
        if (createdLights.empty())
        {
            return;
        }

        lights.push_back(createdLights.front());
        tempLights_.push_back(FTempLight{
            .lightIndex = static_cast<int>(lights.size() - 1),
            .remainingMs = UltimateLightDurationMs,
        });
        sceneDirty_ = true;
    }

    std::vector<FSynergyStatus> FBattleSystem::BuildSynergyPreview() const
    {
        return EvaluateSynergiesFromDeployment();
    }

    std::vector<FSynergyStatus> FBattleSystem::EvaluateSynergiesFromDeployment() const
    {
        std::vector<FSynergyStatus> statuses;
        if (!pieces_ || synergyDefs_.empty())
        {
            return statuses;
        }

        std::unordered_map<std::string, int> counts;
        for (const auto& piece : *pieces_)
        {
            if (piece.def.team != "player" || piece.onBench)
            {
                continue;
            }

            for (const std::string& synergyId : piece.baseDef.synergies)
            {
                ++counts[synergyId];
            }
        }

        for (const FSynergyDef& synergyDef : synergyDefs_)
        {
            const auto countIt = counts.find(synergyDef.id);
            if (countIt == counts.end() || countIt->second <= 0)
            {
                continue;
            }

            FSynergyStatus status{};
            status.id = synergyDef.id;
            status.name = synergyDef.name;
            status.count = countIt->second;

            for (const FSynergyTier& tier : synergyDef.tiers)
            {
                if (status.count >= tier.count)
                {
                    status.active = true;
                    status.activeCount = tier.count;
                    status.activeTier = tier;
                }
                else if (status.nextTierCount == 0 || tier.count < status.nextTierCount)
                {
                    status.nextTierCount = tier.count;
                }
            }

            statuses.push_back(status);
        }

        std::sort(statuses.begin(), statuses.end(), [](const FSynergyStatus& lhs, const FSynergyStatus& rhs)
        {
            if (lhs.active != rhs.active)
            {
                return lhs.active > rhs.active;
            }
            if (lhs.count != rhs.count)
            {
                return lhs.count > rhs.count;
            }
            return lhs.name < rhs.name;
        });
        return statuses;
    }

    void FBattleSystem::ApplySynergyBonuses(const std::vector<FSynergyStatus>& activeSynergies)
    {
        if (!pieces_)
        {
            return;
        }

        for (const FSynergyStatus& synergyStatus : activeSynergies)
        {
            if (!synergyStatus.active)
            {
                continue;
            }

            for (auto& piece : *pieces_)
            {
                if (piece.def.team != "player" || piece.onBench || !PieceHasSynergy(piece, synergyStatus.id))
                {
                    continue;
                }

                ApplyTierToPiece(piece, synergyStatus);
            }
        }

        for (auto& piece : *pieces_)
        {
            if (piece.def.team == "player")
            {
                piece.currentHp = piece.def.hp;
            }
        }
    }

    void FBattleSystem::ApplyTierToPiece(FPieceRuntime& piece, const FSynergyStatus& synergyStatus)
    {
        const FSynergyTier& tier = synergyStatus.activeTier;
        if (tier.atkBonus > 0.0f)
        {
            piece.def.atk = std::max(1, static_cast<int>(std::lround(piece.def.atk * (1.0f + tier.atkBonus))));
        }
        if (tier.hpBonus > 0.0f)
        {
            piece.def.hp = std::max(1, static_cast<int>(std::lround(piece.def.hp * (1.0f + tier.hpBonus))));
        }
        if (tier.spdBonus > 0.0f)
        {
            piece.def.atkSpeed = piece.def.atkSpeed * (1.0f + tier.spdBonus);
        }
        if (tier.apBonus > 0.0f && piece.def.attackType == "ap")
        {
            piece.def.atk = std::max(1, static_cast<int>(std::lround(piece.def.atk * (1.0f + tier.apBonus))));
        }
    }

    void FBattleSystem::Tick()
    {
        if (!pieces_)
        {
            return;
        }

        if (!overtimeActive_ && elapsedMs_ >= BattleLimitMs)
        {
            overtimeActive_ = true;
            overtimeStartMs_ = elapsedMs_;
            spdlog::info("[KongLie3D] Overtime started");
        }

        if (overtimeActive_)
        {
            const float overtimeElapsedSec = std::max(0.0f, (elapsedMs_ - overtimeStartMs_) / 1000.0f);
            dmgMult_ = std::clamp(1.0f + overtimeElapsedSec * 0.1f, 1.0f, 1.7f);
            healMult_ = std::clamp(1.0f - overtimeElapsedSec * 0.1f, 0.3f, 1.0f);
            if (elapsedMs_ >= OvertimeDrawMs)
            {
                state_ = EBattleState::Ended;
                paused_ = false;
                winnerTeam_ = "draw";
                SnapAllPiecesToTargets();
                spdlog::info("[KongLie3D] Battle ended. Winner: draw");
                return;
            }
        }
        else
        {
            dmgMult_ = 1.0f;
            healMult_ = 1.0f;
        }

        for (auto& piece : *pieces_)
        {
            if (!IsCombatPiece(piece))
            {
                continue;
            }

            piece.attackCooldownMs = std::max(0.0f, piece.attackCooldownMs - TickMs);
            piece.healCooldownMs = std::max(0.0f, piece.healCooldownMs - TickMs);
            piece.wCooldownMs = std::max(0.0f, piece.wCooldownMs - TickMs);
            piece.ultimateCooldownMs = std::max(0.0f, piece.ultimateCooldownMs - TickMs);
            piece.shieldTimerMs = std::max(0.0f, piece.shieldTimerMs - TickMs);
            piece.stunTimerMs = std::max(0.0f, piece.stunTimerMs - TickMs);
            piece.furyTimerMs = std::max(0.0f, piece.furyTimerMs - TickMs);
            if (piece.shieldTimerMs <= 0.0f)
            {
                piece.shield = 0;
            }
        }

        for (auto& piece : *pieces_)
        {
            if (!IsCombatPiece(piece))
            {
                continue;
            }

            if (piece.IsStunned())
            {
                continue;
            }

            if (piece.def.role == "support" && TryHeal(piece))
            {
                continue;
            }

            FPieceRuntime* target = FindNearestEnemy(piece);
            if (!target)
            {
                continue;
            }

            if (ChebyshevDistance(piece, *target) <= piece.def.range)
            {
                if (piece.attackCooldownMs <= 0.0f)
                {
                    Attack(piece, *target);
                    piece.attackCooldownMs = ComputeAttackCooldownMs(piece.def.atkSpeed * piece.GetAttackSpeedMultiplier());
                }
            }
            else
            {
                StartMoveTowards(piece, *target);
            }
        }

        ProcessPendingUltimates();

        for (auto& piece : *pieces_)
        {
            if (!IsCombatPiece(piece) || piece.IsStunned() || !piece.def.isHero)
            {
                continue;
            }

            TryCastW(piece, *this);
        }

        int playerAlive = 0;
        int enemyAlive = 0;
        for (const auto& piece : *pieces_)
        {
            if (!IsCombatPiece(piece))
            {
                continue;
            }

            if (piece.def.team == "player")
            {
                ++playerAlive;
            }
            else if (piece.def.team == "enemy")
            {
                ++enemyAlive;
            }
        }

        if (playerAlive == 0 || enemyAlive == 0)
        {
            state_ = EBattleState::Ended;
            paused_ = false;
            if (playerAlive > 0)
            {
                winnerTeam_ = "player";
            }
            else if (enemyAlive > 0)
            {
                winnerTeam_ = "enemy";
            }
            else
            {
                winnerTeam_ = "draw";
            }

            SnapAllPiecesToTargets();
            PlayOutcomeSfx(winnerTeam_);
            spdlog::info("[KongLie3D] Battle ended. Winner: {}", winnerTeam_);
        }
    }

    void FBattleSystem::UpdateMovementInterpolation(float deltaMs)
    {
        if (!pieces_)
        {
            return;
        }

        for (auto& piece : *pieces_)
        {
            if (!piece.node)
            {
                continue;
            }

            if (piece.moveDurationMs <= 0.0f)
            {
                continue;
            }

            piece.moveElapsedMs = std::min(piece.moveElapsedMs + deltaMs, piece.moveDurationMs);
            const float lerpProgress = std::clamp(piece.moveElapsedMs / piece.moveDurationMs, 0.0f, 1.0f);
            const glm::vec3 currentPos = glm::mix(piece.prevWorldPos, piece.targetWorldPos, lerpProgress);
            piece.node->SetTranslation(currentPos);
            piece.node->RecalcTransform(true);
            NextEngine::GetInstance()->GetScene().MarkTransformDirty();

            if (piece.moveElapsedMs >= piece.moveDurationMs)
            {
                piece.moveDurationMs = 0.0f;
                piece.moveElapsedMs = 0.0f;
                piece.prevWorldPos = piece.targetWorldPos;
            }
        }
    }

    void FBattleSystem::UpdateDeathAnimations(float deltaMs)
    {
        if (!pieces_)
        {
            return;
        }

        for (auto& piece : *pieces_)
        {
            if (piece.knockoutTimerMs > 0.0f)
            {
                piece.knockoutTimerMs = std::max(0.0f, piece.knockoutTimerMs - deltaMs);
                if (piece.knockoutTimerMs <= 0.0f && piece.knockoutNode)
                {
                    NodeUtils::SetVisibleRecursive(piece.knockoutNode, false);
                    piece.knockoutNode->SetTranslation(HiddenProxyPosition);
                    piece.knockoutNode->SetRotation(glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
                    piece.knockoutNode->RecalcTransform(true);
                    if (NextPhysics* physics = NextEngine::GetInstance()->GetPhysicsEngine();
                        physics && !piece.knockoutBodyId.IsInvalid())
                    {
                        physics->SetBodyTransform(piece.knockoutBodyId, HiddenProxyPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), true);
                        physics->SetBodyVelocity(piece.knockoutBodyId, glm::vec3(0.0f), glm::vec3(0.0f));
                        physics->SetBodyActive(piece.knockoutBodyId, false);
                    }
                    sceneDirty_ = true;
                }
            }

            if (!piece.node || piece.deathAnimationMs <= 0.0f)
            {
                continue;
            }

            piece.deathAnimationMs = std::min(piece.deathAnimationMs + deltaMs, DeathAnimationDurationMs);
            const float progress = std::clamp(piece.deathAnimationMs / DeathAnimationDurationMs, 0.0f, 1.0f);
            piece.node->SetTranslation(piece.deathStartWorldPos + glm::vec3(0.0f, -DeathSinkDistance * progress, 0.0f));
            piece.node->RecalcTransform(true);
            NextEngine::GetInstance()->GetScene().MarkTransformDirty();

            if (piece.deathAnimationMs >= DeathAnimationDurationMs)
            {
                piece.deathAnimationMs = 0.0f;
                NodeUtils::SetVisibleRecursive(piece.node, false);
                sceneDirty_ = true;
            }
        }
    }

    void FBattleSystem::UpdateAttackTraces(float deltaMs)
    {
        for (auto it = attackTraces_.begin(); it != attackTraces_.end();)
        {
            it->remainingMs -= deltaMs;
            if (it->remainingMs <= 0.0f)
            {
                it = attackTraces_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void FBattleSystem::UpdateSkillEffects(float deltaMs)
    {
        for (auto it = skillEffects_.begin(); it != skillEffects_.end();)
        {
            it->remainingMs -= deltaMs;
            if (it->remainingMs <= 0.0f)
            {
                it = skillEffects_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void FBattleSystem::UpdateDamagePopups(float deltaMs)
    {
        for (auto it = damagePopups_.begin(); it != damagePopups_.end();)
        {
            it->remainingMs -= deltaMs;
            if (it->remainingMs <= 0.0f)
            {
                it = damagePopups_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void FBattleSystem::UpdateProjectiles(float deltaMs)
    {
        for (auto it = projectiles_.begin(); it != projectiles_.end();)
        {
            if (!it->node)
            {
                it = projectiles_.erase(it);
                continue;
            }

            it->elapsedMs = std::min(it->elapsedMs + deltaMs, it->durationMs);
            const float progress = std::clamp(it->elapsedMs / std::max(it->durationMs, 1.0f), 0.0f, 1.0f);
            const glm::vec3 currentPos = glm::mix(it->startPos, it->endPos, progress);
            it->node->SetTranslation(currentPos);
            it->node->RecalcTransform(true);
            NextEngine::GetInstance()->GetScene().MarkTransformDirty();

            if (it->elapsedMs >= it->durationMs)
            {
                if (it->onHit)
                {
                    it->onHit();
                }
                if (it->poolIndex < projectilePool_.size())
                {
                    projectilePool_[it->poolIndex].active = false;
                    NodeUtils::SetVisibleRecursive(projectilePool_[it->poolIndex].node, false);
                    sceneDirty_ = true;
                }
                it = projectiles_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void FBattleSystem::UpdateImpactDebris(float deltaMs)
    {
        for (auto it = impactDebris_.begin(); it != impactDebris_.end();)
        {
            if (!it->node)
            {
                it = impactDebris_.erase(it);
                continue;
            }

            it->elapsedMs = std::min(it->elapsedMs + deltaMs, it->durationMs);
            const float progress = std::clamp(it->elapsedMs / std::max(it->durationMs, 1.0f), 0.0f, 1.0f);
            const glm::vec3 horizontalOffset(it->velocity.x * progress, 0.0f, it->velocity.z * progress);
            const float verticalOffset = it->velocity.y * std::sin(progress * glm::pi<float>()) - 0.12f * progress;
            const glm::vec3 currentPos = it->startPos + horizontalOffset + glm::vec3(0.0f, verticalOffset, 0.0f);
            const float scale = std::max(0.18f * it->startScale, it->startScale * (1.0f - progress * 0.82f));
            it->node->SetTranslation(currentPos);
            it->node->SetScale(glm::vec3(scale));
            it->node->RecalcTransform(true);
            NextEngine::GetInstance()->GetScene().MarkTransformDirty();

            if (it->elapsedMs >= it->durationMs)
            {
                if (it->poolIndex < debrisPool_.size())
                {
                    debrisPool_[it->poolIndex].active = false;
                    NodeUtils::SetVisibleRecursive(debrisPool_[it->poolIndex].node, false);
                    sceneDirty_ = true;
                }
                it = impactDebris_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void FBattleSystem::UpdateHitFlashes(float deltaMs)
    {
        if (!pieces_ || hitFlashMaterialId_ == 0)
        {
            return;
        }

        for (auto& piece : *pieces_)
        {
            if (!piece.hitFlashActive)
            {
                continue;
            }

            piece.hitFlashMs = std::max(0.0f, piece.hitFlashMs - deltaMs);
            if (piece.hitFlashMs <= 0.0f)
            {
                piece.hitFlashActive = false;
                if (piece.alive)
                {
                    RestorePieceMaterials(piece);
                    sceneDirty_ = true;
                }
            }
        }
    }

    void FBattleSystem::UpdateUltimatePresentation(float deltaMs)
    {
        ultimatePresentation_.flashRemainingMs = std::max(0.0f, ultimatePresentation_.flashRemainingMs - deltaMs);
        ultimatePresentation_.cameraFocusRemainingMs = std::max(0.0f, ultimatePresentation_.cameraFocusRemainingMs - deltaMs);
        ultimatePresentation_.titleRemainingMs = std::max(0.0f, ultimatePresentation_.titleRemainingMs - deltaMs);
        if (ultimatePresentation_.titleRemainingMs <= 0.0f)
        {
            ultimatePresentation_.title.clear();
        }
    }

    void FBattleSystem::SnapAllPiecesToTargets()
    {
        if (!pieces_)
        {
            return;
        }

        for (auto& piece : *pieces_)
        {
            piece.moveDurationMs = 0.0f;
            piece.moveElapsedMs = 0.0f;
            piece.prevWorldPos = piece.targetWorldPos;
            if (piece.node)
            {
                piece.node->SetTranslation(piece.targetWorldPos);
                piece.node->RecalcTransform(true);
            }
        }
        sceneDirty_ = true;
    }

    FPieceRuntime* FBattleSystem::FindNearestEnemy(FPieceRuntime& piece)
    {
        if (!pieces_)
        {
            return nullptr;
        }

        FPieceRuntime* bestTarget = nullptr;
        int bestDistance = std::numeric_limits<int>::max();
        for (auto& candidate : *pieces_)
        {
            if (!IsCombatPiece(candidate) || candidate.def.team == piece.def.team)
            {
                continue;
            }

            const int distance = ChebyshevDistance(piece, candidate);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestTarget = &candidate;
            }
        }
        return bestTarget;
    }

    FPieceRuntime* FBattleSystem::FindLowestHealthAlly(FPieceRuntime& piece)
    {
        if (!pieces_)
        {
            return nullptr;
        }

        FPieceRuntime* lowestHealthAlly = nullptr;
        float lowestHealthRatio = 1.0f;
        for (auto& candidate : *pieces_)
        {
            if (!IsCombatPiece(candidate) || &candidate == &piece || candidate.def.team != piece.def.team)
            {
                continue;
            }

            if (candidate.currentHp >= candidate.def.hp)
            {
                continue;
            }

            const float healthRatio = static_cast<float>(candidate.currentHp) / static_cast<float>(candidate.def.hp);
            if (!lowestHealthAlly || healthRatio < lowestHealthRatio)
            {
                lowestHealthAlly = &candidate;
                lowestHealthRatio = healthRatio;
            }
        }
        return lowestHealthAlly;
    }

    void FBattleSystem::Attack(FPieceRuntime& attacker, FPieceRuntime& target)
    {
        if (!attacker.node || !target.node)
        {
            return;
        }

        float damageScale = dmgMult_ * attacker.GetDamageMultiplier();
        if (attacker.def.team == "enemy")
        {
            damageScale *= enemyDamageMultiplier_;
        }
        const int damage = std::max(1, static_cast<int>(std::lround(static_cast<float>(attacker.def.atk) * damageScale)));
        if (attacker.def.maxMana > 0 && attacker.def.manaPerAtk > 0)
        {
            attacker.currentMana = std::min(attacker.def.maxMana, attacker.currentMana + attacker.def.manaPerAtk);
        }

        const glm::vec3 attackStart = attacker.node->WorldTranslation() + glm::vec3(0.0f, 0.3f, 0.0f);
        const glm::vec3 attackEnd = target.node->WorldTranslation() + glm::vec3(0.0f, 0.3f, 0.0f);
        const glm::vec4 attackColor = attacker.def.attackType == "ap" ? glm::vec4(0.30f, 0.62f, 1.0f, 1.0f)
                                                                      : glm::vec4(1.0f, 0.58f, 0.20f, 1.0f);
        const auto applyAttackHit = [&attacker, &target, damage, this]()
        {
            if (!target.alive || !target.node)
            {
                return;
            }

            target.lastAttackerPos = attacker.node ? attacker.node->WorldTranslation() : attacker.targetWorldPos;
            const int hpDamage = target.ApplyDamage(damage);
            const int shieldAbsorbed = damage - hpDamage;
            if (shieldAbsorbed > 0)
            {
                spdlog::info("[KongLie3D] [shield absorbed {}] {}", shieldAbsorbed, target.pieceId);
            }

            if (attacker.def.attackType == "ap")
            {
                attacker.statDmgAP += hpDamage;
            }
            else
            {
                attacker.statDmgAD += hpDamage;
            }

            PlayAttackHitSfx(attacker.def.attackType);
            TriggerHitFeedback(target, target.node->WorldTranslation() + glm::vec3(0.0f, 0.28f, 0.0f));
            if (hpDamage > 0)
            {
                RecordDamagePopup(target.node->WorldTranslation() +
                                      glm::vec3(0.0f, target.dimensions.y * target.visualScale + 0.2f, 0.0f),
                                  fmt::format("-{}", hpDamage),
                                  glm::vec4(1.0f, 0.30f, 0.24f, 1.0f));
            }

            if (target.currentHp <= 0)
            {
                KillPiece(target);
            }
        };

        if (attacker.def.range >= 2)
        {
            const float projectileDurationMs = std::min(MaxProjectileDurationMs,
                                                        80.0f * static_cast<float>(ChebyshevDistance(attacker, target)));
            const bool spawned = SpawnProjectile(attacker.def.attackType == "ap" ? EProjectileKind::AttackAP : EProjectileKind::AttackAD,
                                                 attackStart,
                                                 attackEnd,
                                                 std::max(80.0f, projectileDurationMs),
                                                 glm::vec3(attackColor),
                                                 applyAttackHit);
            if (spawned)
            {
                return;
            }
        }

        RecordAttackTrace(attackStart, attackEnd, attackColor);
        applyAttackHit();
    }

    bool FBattleSystem::TryHeal(FPieceRuntime& healer)
    {
        if (healer.healCooldownMs > 0.0f || healer.def.healAmount <= 0 || healer.def.healInterval <= 0)
        {
            return false;
        }

        FPieceRuntime* target = FindLowestHealthAlly(healer);
        if (!target || !healer.node || !target->node)
        {
            return false;
        }

        const int missingHealth = target->def.hp - target->currentHp;
        if (missingHealth <= 0)
        {
            return false;
        }

        const int healedAmount = std::min(std::max(1, static_cast<int>(std::lround(healer.def.healAmount * healMult_))), missingHealth);
        healer.healCooldownMs = static_cast<float>(healer.def.healInterval);
        const glm::vec3 healStart = healer.node->WorldTranslation() + glm::vec3(0.0f, 0.25f, 0.0f);
        const glm::vec3 healEnd = target->node->WorldTranslation() + glm::vec3(0.0f, 0.25f, 0.0f);

        const auto applyHeal = [target, &healer, healedAmount]()
        {
            if (!target->alive)
            {
                return;
            }

            target->currentHp = std::min(target->def.hp, target->currentHp + healedAmount);
            healer.statHeal += healedAmount;
        };

        if (healer.def.range >= 2)
        {
            const float projectileDurationMs =
                std::min(MaxProjectileDurationMs, 80.0f * static_cast<float>(ChebyshevDistance(healer, *target)));
            if (SpawnProjectile(EProjectileKind::Heal,
                                healStart,
                                healEnd,
                                std::max(80.0f, projectileDurationMs),
                                glm::vec3(0.30f, 1.0f, 0.40f),
                                applyHeal))
            {
                return true;
            }
        }

        applyHeal();
        RecordAttackTrace(healStart, healEnd, glm::vec4(0.3f, 1.0f, 0.4f, 1.0f));
        return true;
    }

    void FBattleSystem::StartMoveTowards(FPieceRuntime& piece, const FPieceRuntime& target)
    {
        const int nextCol = StepTowards(piece.col, target.col);
        const int nextRow = StepTowards(piece.row, target.row);
        if (nextCol == piece.col && nextRow == piece.row)
        {
            return;
        }

        piece.prevWorldPos = piece.node ? piece.node->Translation() : ComputeWorldPosition(piece);
        piece.col = nextCol;
        piece.row = nextRow;
        piece.targetWorldPos = ComputeWorldPosition(piece);
        piece.moveElapsedMs = 0.0f;
        piece.moveDurationMs = TickMs / std::max(piece.def.moveSpeed, MinMoveSpeed);
    }

    void FBattleSystem::KillPiece(FPieceRuntime& piece)
    {
        piece.alive = false;
        piece.currentHp = 0;
        piece.attackCooldownMs = 0.0f;
        piece.healCooldownMs = 0.0f;
        piece.moveCooldownMs = 0.0f;
        piece.moveDurationMs = 0.0f;
        piece.moveElapsedMs = 0.0f;
        piece.wCooldownMs = 0.0f;
        piece.ultimateCooldownMs = 0.0f;
        piece.shield = 0;
        piece.shieldTimerMs = 0.0f;
        piece.stunTimerMs = 0.0f;
        piece.furyTimerMs = 0.0f;
        piece.hitFlashMs = 0.0f;
        piece.hitFlashActive = false;
        piece.deathStartWorldPos = piece.node ? piece.node->WorldTranslation() : piece.targetWorldPos;
        piece.deathAnimationMs = 0.0f;
        piece.knockoutTimerMs = 0.0f;
        PlayUnitDieSfx();

        if (piece.node)
        {
            RestorePieceMaterials(piece);
            NodeUtils::SetVisibleRecursive(piece.node, false);
            sceneDirty_ = true;
        }

        bool spawnedKnockoutProxy = false;
        if (piece.knockoutNode && !piece.knockoutBodyId.IsInvalid())
        {
            if (NextPhysics* physics = NextEngine::GetInstance()->GetPhysicsEngine())
            {
                const glm::vec3 fallbackDir = piece.def.team == "player" ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 impactDir = piece.deathStartWorldPos - piece.lastAttackerPos;
                if (glm::length(impactDir) < 0.001f)
                {
                    impactDir = fallbackDir;
                }
                impactDir = glm::normalize(impactDir);
                const glm::vec3 launchDir = glm::normalize(impactDir + glm::vec3(0.0f, 0.14f, 0.0f));
                const glm::quat startRotation = piece.node ? piece.node->WorldRotation() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

                piece.knockoutNode->SetScale(piece.dimensions * piece.visualScale);
                piece.knockoutNode->SetTranslation(piece.deathStartWorldPos);
                piece.knockoutNode->SetRotation(startRotation);
                piece.knockoutNode->RecalcTransform(true);
                NodeUtils::SetVisibleRecursive(piece.knockoutNode, true);

                physics->SetBodyTransform(piece.knockoutBodyId, piece.deathStartWorldPos, startRotation, true);
                physics->SetBodyVelocity(piece.knockoutBodyId,
                                         impactDir * 1.35f + glm::vec3(0.0f, 0.72f, 0.0f),
                                         glm::vec3(launchDir.z * 1.75f, 2.15f, -launchDir.x * 1.75f));
                physics->SetBodyActive(piece.knockoutBodyId, true);
                physics->AddForceToBody(piece.knockoutBodyId, (impactDir + glm::vec3(0.0f, 0.05f, 0.0f)) * 4200.0f);

                piece.knockoutTimerMs = KnockoutProxyDurationMs;
                SpawnImpactDebris(piece.deathStartWorldPos + glm::vec3(0.0f, 0.10f, 0.0f), DebrisPerDeath, 2.0f, 0.75f);
                spawnedKnockoutProxy = true;
                sceneDirty_ = true;
            }
        }

        if (!spawnedKnockoutProxy)
        {
            piece.deathAnimationMs = 1.0f;
            if (piece.node)
            {
                NodeUtils::SetVisibleRecursive(piece.node, true);
                NodeUtils::SetMaterialRecursive(piece.node, piece.darkMaterialId);
            }
        }
    }

    void FBattleSystem::RecordAttackTrace(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color)
    {
        attackTraces_.push_back(FAttackTrace{
            .from = from,
            .to = to,
            .color = color,
            .remainingMs = 100.0f,
        });
    }

    void FBattleSystem::RecordDamagePopup(const glm::vec3& worldPos, const std::string& text, const glm::vec4& color)
    {
        damagePopups_.push_back(FDamagePopup{
            .worldPos = worldPos,
            .text = text,
            .color = color,
            .durationMs = DamagePopupLifetimeMs,
            .remainingMs = DamagePopupLifetimeMs,
        });
    }

    FPieceRuntime* FBattleSystem::FindNearestEnemyForSkill(FPieceRuntime& piece)
    {
        return FindNearestEnemy(piece);
    }

    FPieceRuntime* FBattleSystem::FindFarthestEnemyForSkill(FPieceRuntime& piece)
    {
        if (!pieces_)
        {
            return nullptr;
        }

        FPieceRuntime* bestTarget = nullptr;
        int bestDistance = -1;
        for (auto& candidate : *pieces_)
        {
            if (!IsCombatPiece(candidate) || candidate.def.team == piece.def.team)
            {
                continue;
            }

            const int distance = ChebyshevDistance(piece, candidate);
            if (distance > bestDistance)
            {
                bestDistance = distance;
                bestTarget = &candidate;
            }
        }
        return bestTarget;
    }

    FPieceRuntime* FBattleSystem::FindNearestAllyForSkill(FPieceRuntime& piece)
    {
        if (!pieces_)
        {
            return nullptr;
        }

        FPieceRuntime* bestTarget = nullptr;
        int bestDistance = std::numeric_limits<int>::max();
        for (auto& candidate : *pieces_)
        {
            if (!IsCombatPiece(candidate) || &candidate == &piece || candidate.def.team != piece.def.team)
            {
                continue;
            }

            const int distance = ChebyshevDistance(piece, candidate);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestTarget = &candidate;
            }
        }
        return bestTarget;
    }

    bool FBattleSystem::FindAdjacentCellForSkill(const FPieceRuntime& mover, const FPieceRuntime& target, glm::ivec2& outCell) const
    {
        if (!pieces_)
        {
            return false;
        }

        bool found = false;
        int bestDistance = std::numeric_limits<int>::max();
        for (int rowOffset = -1; rowOffset <= 1; ++rowOffset)
        {
            for (int colOffset = -1; colOffset <= 1; ++colOffset)
            {
                if (rowOffset == 0 && colOffset == 0)
                {
                    continue;
                }

                const int col = target.col + colOffset;
                const int row = target.row + rowOffset;
                if (col < 0 || col >= 7 || row < 0 || row >= 8)
                {
                    continue;
                }

                bool occupied = false;
                for (const auto& candidate : *pieces_)
                {
                    if (!IsCombatPiece(candidate) || &candidate == &mover)
                    {
                        continue;
                    }

                    if (candidate.col == col && candidate.row == row)
                    {
                        occupied = true;
                        break;
                    }
                }

                if (occupied)
                {
                    continue;
                }

                const int distance = std::max(std::abs(mover.col - col), std::abs(mover.row - row));
                if (!found || distance < bestDistance)
                {
                    found = true;
                    bestDistance = distance;
                    outCell = glm::ivec2(col, row);
                }
            }
        }

        return found;
    }

    void FBattleSystem::TeleportPiece(FPieceRuntime& piece, int col, int row)
    {
        piece.col = col;
        piece.row = row;
        piece.prevWorldPos = ComputeWorldPosition(piece);
        piece.targetWorldPos = piece.prevWorldPos;
        piece.moveDurationMs = 0.0f;
        piece.moveElapsedMs = 0.0f;
        if (piece.node)
        {
            piece.node->SetTranslation(piece.targetWorldPos);
            piece.node->RecalcTransform(true);
        }
        sceneDirty_ = true;
    }

    void FBattleSystem::ApplyAbilityDamage(FPieceRuntime& attacker, FPieceRuntime& target, int damage, const glm::vec4& color,
                                           const char* sourceTag)
    {
        float scaledDamage = static_cast<float>(damage);
        if (attacker.def.team == "enemy")
        {
            scaledDamage *= enemyDamageMultiplier_;
        }
        const int clampedDamage = std::max(1, static_cast<int>(std::lround(scaledDamage)));
        target.lastAttackerPos = attacker.node ? attacker.node->WorldTranslation() : attacker.targetWorldPos;
        const int hpDamage = target.ApplyDamage(clampedDamage);
        const int shieldAbsorbed = clampedDamage - hpDamage;
        if (shieldAbsorbed > 0)
        {
            spdlog::info("[KongLie3D] [shield absorbed {}] {}", shieldAbsorbed, target.pieceId);
        }

        if (attacker.def.attackType == "ap")
        {
            attacker.statDmgAP += hpDamage;
        }
        else
        {
            attacker.statDmgAD += hpDamage;
        }

        if (attacker.node && target.node)
        {
            RecordAttackTrace(attacker.node->WorldTranslation() + glm::vec3(0.0f, 0.32f, 0.0f),
                              target.node->WorldTranslation() + glm::vec3(0.0f, 0.32f, 0.0f),
                              color);
            TriggerHitFeedback(target, target.node->WorldTranslation() + glm::vec3(0.0f, 0.28f, 0.0f));
            if (hpDamage > 0)
            {
                RecordDamagePopup(target.node->WorldTranslation() +
                                      glm::vec3(0.0f, target.dimensions.y * target.visualScale + 0.2f, 0.0f),
                                  fmt::format("-{}", hpDamage),
                                  glm::vec4(1.0f, 0.30f, 0.24f, 1.0f));
            }
        }

        spdlog::info("[KongLie3D] {} hit {} with {}", attacker.pieceId, target.pieceId, sourceTag);

        if (target.currentHp <= 0)
        {
            KillPiece(target);
        }
    }

    void FBattleSystem::TriggerHitFeedback(FPieceRuntime& target, const glm::vec3& impactPos)
    {
        target.hitFlashMs = HitFlashDurationMs;
        if (!target.hitFlashActive && hitFlashMaterialId_ != 0)
        {
            target.hitFlashActive = true;
            NodeUtils::SetMaterialRecursive(target.node, hitFlashMaterialId_);
            sceneDirty_ = true;
        }

        screenShakeMs_ = std::min(MaxScreenShakeMs, screenShakeMs_ + ScreenShakePerHitMs);
        SpawnImpactDebris(impactPos);
    }

    bool FBattleSystem::SpawnProjectile(EProjectileKind kind,
                                        const glm::vec3& startPos,
                                        const glm::vec3& endPos,
                                        float durationMs,
                                        const glm::vec3& color,
                                        std::function<void()> onHit)
    {
        for (size_t index = 0; index < projectilePool_.size(); ++index)
        {
            auto& pooledProjectile = projectilePool_[index];
            if (pooledProjectile.active || pooledProjectile.kind != kind || !pooledProjectile.node)
            {
                continue;
            }

            pooledProjectile.active = true;
            pooledProjectile.node->SetScale(glm::vec3(1.0f));
            pooledProjectile.node->SetTranslation(startPos);
            pooledProjectile.node->RecalcTransform(true);
            NodeUtils::SetVisibleRecursive(pooledProjectile.node, true);
            sceneDirty_ = true;

            projectiles_.push_back(FProjectile{
                .startPos = startPos,
                .endPos = endPos,
                .durationMs = durationMs,
                .elapsedMs = 0.0f,
                .color = color,
                .modelId = pooledProjectile.modelId,
                .materialId = pooledProjectile.materialId,
                .node = pooledProjectile.node,
                .onHit = std::move(onHit),
                .poolIndex = index,
            });
            return true;
        }

        return false;
    }

    void FBattleSystem::SpawnImpactDebris(const glm::vec3& impactPos, int count, float startScale, float speedScale)
    {
        int spawned = 0;
        for (size_t index = 0; index < debrisPool_.size() && spawned < count; ++index)
        {
            auto& pooledDebris = debrisPool_[index];
            if (pooledDebris.active || !pooledDebris.node)
            {
                continue;
            }

            pooledDebris.active = true;
            pooledDebris.node->SetScale(glm::vec3(startScale));
            pooledDebris.node->SetTranslation(impactPos);
            pooledDebris.node->RecalcTransform(true);
            NodeUtils::SetVisibleRecursive(pooledDebris.node, true);
            sceneDirty_ = true;

            impactDebris_.push_back(FImpactDebris{
                .node = pooledDebris.node,
                .startPos = impactPos,
                .velocity = ComputeImpactDebrisVelocity(static_cast<size_t>(spawned), speedScale),
                .startScale = startScale,
                .durationMs = DebrisDurationMs,
                .elapsedMs = 0.0f,
                .poolIndex = index,
            });
            ++spawned;
        }
    }

    void FBattleSystem::PushSkillEffect(const FSkillEffect& effect)
    {
        skillEffects_.push_back(effect);
    }

    const FRelicDef* FBattleSystem::GetSelectedRelic() const
    {
        if (selectedRelicIndex_ < 0 || selectedRelicIndex_ >= static_cast<int>(relics_.size()))
        {
            return nullptr;
        }
        return &relics_[selectedRelicIndex_];
    }

    void FBattleSystem::ApplySelectedRelic()
    {
        if (!pieces_)
        {
            return;
        }

        const FRelicDef* relic = GetSelectedRelic();
        for (auto& piece : *pieces_)
        {
            piece.def = piece.baseDef;
            if (piece.def.team != "player")
            {
                continue;
            }

            if (relic)
            {
                if (relic->statKey == "atkBonus" && piece.def.attackType == "ad")
                {
                    piece.def.atk = std::max(1, static_cast<int>(std::lround(piece.baseDef.atk * (1.0f + relic->statVal))));
                }
                else if (relic->statKey == "apBonus" && piece.def.attackType == "ap")
                {
                    piece.def.atk = std::max(1, static_cast<int>(std::lround(piece.baseDef.atk * (1.0f + relic->statVal))));
                }
                else if (relic->statKey == "spdBonus")
                {
                    piece.def.atkSpeed = piece.baseDef.atkSpeed * (1.0f + relic->statVal);
                }
                else if (relic->statKey == "hpBonus")
                {
                    piece.def.hp = std::max(1, static_cast<int>(std::lround(piece.baseDef.hp * (1.0f + relic->statVal))));
                }
                else if (relic->statKey == "cdBonus")
                {
                    piece.def.skillWCooldownMs =
                        std::max(100, static_cast<int>(std::lround(piece.baseDef.skillWCooldownMs * (1.0f - relic->statVal))));
                }
            }

            piece.currentHp = piece.def.hp;
        }
    }

    void FBattleSystem::UpdateTempLights(float deltaMs)
    {
        if (tempLights_.empty())
        {
            return;
        }

        NextEngine* engine = NextEngine::GetInstance();
        if (!engine)
        {
            tempLights_.clear();
            return;
        }

        auto& lights = engine->GetScene().Lights();
        bool removedAny = false;
        for (size_t index = 0; index < tempLights_.size();)
        {
            tempLights_[index].remainingMs -= deltaMs;
            if (tempLights_[index].remainingMs > 0.0f)
            {
                ++index;
                continue;
            }

            const int lightIndex = tempLights_[index].lightIndex;
            if (lightIndex >= 0 && lightIndex < static_cast<int>(lights.size()))
            {
                lights.erase(lights.begin() + lightIndex);
                for (auto& tempLight : tempLights_)
                {
                    if (tempLight.lightIndex > lightIndex)
                    {
                        --tempLight.lightIndex;
                    }
                }
                removedAny = true;
            }

            tempLights_.erase(tempLights_.begin() + static_cast<std::ptrdiff_t>(index));
        }

        if (removedAny)
        {
            sceneDirty_ = true;
        }
    }

    uint32_t FBattleSystem::EnsureTempLightMaterial(const glm::vec3& color)
    {
        NextEngine* engine = NextEngine::GetInstance();
        auto& materials = engine->GetScene().Materials();
        uint32_t& cachedMaterialId = color.b >= color.r ? blueUltimateLightMaterialId_ : orangeUltimateLightMaterialId_;
        if (cachedMaterialId != std::numeric_limits<uint32_t>::max() && cachedMaterialId < materials.size())
        {
            return cachedMaterialId;
        }

        cachedMaterialId = SceneBuilder::AddDiffuseLightMaterial(materials, color, 24.0f);
        return cachedMaterialId;
    }

    FPieceRuntime* FBattleSystem::FindPieceById(const std::string& pieceId)
    {
        if (!pieces_)
        {
            return nullptr;
        }

        for (auto& piece : *pieces_)
        {
            if (piece.pieceId == pieceId)
            {
                return &piece;
            }
        }

        return nullptr;
    }

    bool FBattleSystem::IsUltimateReady(const FPieceRuntime& piece) const
    {
        return piece.def.isHero && piece.alive && !piece.onBench && !piece.ultimateUsed && piece.def.maxMana > 0 &&
               piece.currentMana >= piece.def.maxMana && piece.ultimateCooldownMs <= 0.0f;
    }

    void FBattleSystem::ProcessPendingUltimates()
    {
        if (pendingUltimateIds_.empty())
        {
            return;
        }

        std::vector<std::string> remaining;
        remaining.reserve(pendingUltimateIds_.size());
        for (const std::string& heroId : pendingUltimateIds_)
        {
            FPieceRuntime* piece = FindPieceById(heroId);
            if (!piece || !IsUltimateReady(*piece) || !CastUltimate(*piece, *this))
            {
                if (piece && !piece->ultimateUsed)
                {
                    remaining.push_back(heroId);
                }
            }
        }
        pendingUltimateIds_ = std::move(remaining);
    }
}
