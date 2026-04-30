#include "KongLie3DBattleSystem.hpp"

#include "Assets/Core/Node.h"
#include "KongLie3DSkills.hpp"
#include "Runtime/Components/RenderComponent.h"

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
    constexpr float AttackCooldownScale = 1.2f;

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
        return (baseCooldownMs + TickMs) * AttackCooldownScale;
    }

    void SetNodeVisibilityRecursive(const std::shared_ptr<Assets::Node>& node, bool visible)
    {
        if (!node)
        {
            return;
        }

        if (auto renderComponent = node->GetComponent<Runtime::RenderComponent>())
        {
            renderComponent->SetVisible(visible);
        }

        for (const auto& child : node->Children())
        {
            SetNodeVisibilityRecursive(child, visible);
        }
    }

    void SetNodeMaterialRecursive(const std::shared_ptr<Assets::Node>& node, uint32_t materialId)
    {
        if (!node)
        {
            return;
        }

        if (auto renderComponent = node->GetComponent<Runtime::RenderComponent>())
        {
            auto materials = renderComponent->Materials();
            materials[0] = materialId;
            renderComponent->SetMaterial(materials);
        }

        for (const auto& child : node->Children())
        {
            SetNodeMaterialRecursive(child, materialId);
        }
    }
}

namespace KongLie3D
{
    void FBattleSystem::BindPieces(std::vector<FPieceRuntime>* pieces)
    {
        pieces_ = pieces;
        Reset();
    }

    void FBattleSystem::SetRelics(std::vector<FRelicDef> relics)
    {
        relics_ = std::move(relics);
        if (selectedRelicIndex_ < 0 || selectedRelicIndex_ >= static_cast<int>(relics_.size()))
        {
            selectedRelicIndex_ = -1;
        }
    }

    void FBattleSystem::Reset()
    {
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
        sceneDirty_ = true;

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
            piece.deathAnimationMs = 0.0f;
            piece.statDmgAD = 0;
            piece.statDmgAP = 0;
            piece.statDmgTaken = 0;
            piece.statHeal = 0;
            piece.prevWorldPos = ComputeWorldPosition(piece);
            piece.targetWorldPos = piece.prevWorldPos;
            if (piece.node)
            {
                SetNodeMaterialRecursive(piece.node, piece.materialId);
                piece.node->SetTranslation(piece.targetWorldPos);
                piece.node->RecalcTransform(true);
            }
            SetNodeVisibilityRecursive(piece.node, true);
        }
    }

    void FBattleSystem::Start()
    {
        if (!pieces_ || state_ != EBattleState::Deployment)
        {
            return;
        }

        ApplySelectedRelic();

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

        const float deltaMs = static_cast<float>(deltaSeconds * 1000.0);
        UpdateAttackTraces(deltaMs);
        UpdateSkillEffects(deltaMs);
        UpdateDamagePopups(deltaMs);

        if (state_ == EBattleState::Battle)
        {
            elapsedMs_ += deltaMs;
            tickAccumulatorMs_ += deltaMs;

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

        UpdateMovementInterpolation(deltaMs);
        UpdateDeathAnimations(deltaMs);
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
            sceneDirty_ = true;

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
            if (!piece.node || piece.deathAnimationMs <= 0.0f)
            {
                continue;
            }

            piece.deathAnimationMs = std::min(piece.deathAnimationMs + deltaMs, DeathAnimationDurationMs);
            const float progress = std::clamp(piece.deathAnimationMs / DeathAnimationDurationMs, 0.0f, 1.0f);
            piece.node->SetTranslation(piece.deathStartWorldPos + glm::vec3(0.0f, -DeathSinkDistance * progress, 0.0f));
            piece.node->RecalcTransform(true);
            sceneDirty_ = true;

            if (piece.deathAnimationMs >= DeathAnimationDurationMs)
            {
                piece.deathAnimationMs = 0.0f;
                SetNodeVisibilityRecursive(piece.node, false);
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

        const int damage = std::max(
            1, static_cast<int>(std::lround(static_cast<float>(attacker.def.atk) * dmgMult_ * attacker.GetDamageMultiplier())));
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

        if (attacker.def.maxMana > 0 && attacker.def.manaPerAtk > 0)
        {
            attacker.currentMana = std::min(attacker.def.maxMana, attacker.currentMana + attacker.def.manaPerAtk);
        }

        RecordAttackTrace(attacker.node->WorldTranslation() + glm::vec3(0.0f, 0.3f, 0.0f),
                          target.node->WorldTranslation() + glm::vec3(0.0f, 0.3f, 0.0f),
                          attacker.def.attackType == "ap" ? glm::vec4(0.30f, 0.62f, 1.0f, 1.0f)
                                                          : glm::vec4(1.0f, 0.58f, 0.20f, 1.0f));
        if (hpDamage > 0)
        {
            RecordDamagePopup(target.node->WorldTranslation() + glm::vec3(0.0f, target.dimensions.y * target.visualScale + 0.2f, 0.0f),
                              fmt::format("-{}", hpDamage),
                              glm::vec4(1.0f, 0.30f, 0.24f, 1.0f));
        }

        if (target.currentHp <= 0)
        {
            KillPiece(target);
        }
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
        target->currentHp += healedAmount;
        healer.statHeal += healedAmount;
        healer.healCooldownMs = static_cast<float>(healer.def.healInterval);

        RecordAttackTrace(healer.node->WorldTranslation() + glm::vec3(0.0f, 0.25f, 0.0f),
                          target->node->WorldTranslation() + glm::vec3(0.0f, 0.25f, 0.0f),
                          glm::vec4(0.3f, 1.0f, 0.4f, 1.0f));
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
        piece.deathAnimationMs = 1.0f;
        piece.deathStartWorldPos = piece.node ? piece.node->Translation() : piece.targetWorldPos;
        SetNodeMaterialRecursive(piece.node, piece.darkMaterialId);
        sceneDirty_ = true;
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
        const int clampedDamage = std::max(1, damage);
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
