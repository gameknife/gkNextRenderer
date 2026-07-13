#include "KongLie3DSkills.hpp"

#include "KongLie3DAudio.hpp"
#include "KongLie3DBattleSystem.hpp"
#include "Engine/Assets/Core/Node.hpp"

#include <spdlog/spdlog.h>

namespace
{
    constexpr int ShieldAmount = 150;
    constexpr float ShieldDurationMs = 3000.0f;
    constexpr int RapidSlashDamage = 150;
    constexpr float RapidSlashStunMs = 500.0f;
    constexpr int BlueSurgeDamage = 160;
    constexpr float BlueSurgeDurationMs = 400.0f;
    constexpr float FuryDurationMs = 4000.0f;
}

namespace KongLie3D
{
    bool TryCastW(FPieceRuntime& piece, FBattleSystem& battleSystem)
    {
        if (!piece.def.isHero || !piece.alive || piece.onBench || piece.IsStunned() || piece.currentMana < piece.def.maxMana ||
            piece.wCooldownMs > 0.0f)
        {
            return false;
        }

        if (piece.def.skillW == "magic_shield")
        {
            piece.currentMana = 0;
            piece.wCooldownMs = static_cast<float>(piece.def.skillWCooldownMs);
            piece.shield += ShieldAmount;
            piece.shieldTimerMs = ShieldDurationMs;
            PlaySkillCastSfx();

            if (FPieceRuntime* ally = battleSystem.FindNearestAllyForSkill(piece))
            {
                ally->shield += ShieldAmount;
                ally->shieldTimerMs = ShieldDurationMs;
                spdlog::info("[KongLie3D] {} cast W magic_shield on self + {}", piece.pieceId, ally->pieceId);
            }
            else
            {
                spdlog::info("[KongLie3D] {} cast W magic_shield on self", piece.pieceId);
            }
            return true;
        }

        if (piece.def.skillW == "rapid_slash")
        {
            FPieceRuntime* target = battleSystem.FindFarthestEnemyForSkill(piece);
            if (!target)
            {
                return false;
            }

            const glm::vec3 origin = piece.node ? piece.node->WorldTranslation() : piece.targetWorldPos;
            glm::ivec2 adjacentCell{};
            if (battleSystem.FindAdjacentCellForSkill(piece, *target, adjacentCell))
            {
                battleSystem.TeleportPiece(piece, adjacentCell.x, adjacentCell.y);
            }

            piece.currentMana = 0;
            piece.wCooldownMs = static_cast<float>(piece.def.skillWCooldownMs);
            target->stunTimerMs = std::max(target->stunTimerMs, RapidSlashStunMs);
            PlaySkillCastSfx();
            battleSystem.ApplyAbilityDamage(piece, *target, RapidSlashDamage, glm::vec4(1.0f, 0.52f, 0.16f, 1.0f), "rapid_slash");
            battleSystem.PushSkillEffect(FSkillEffect{
                .type = ESkillEffectType::Beam,
                .from = origin,
                .to = piece.node ? piece.node->WorldTranslation() : piece.targetWorldPos,
                .color = glm::vec4(1.0f, 0.55f, 0.18f, 1.0f),
                .durationMs = 220.0f,
                .remainingMs = 220.0f,
            });
            spdlog::info("[KongLie3D] {} cast W rapid_slash on {}", piece.pieceId, target->pieceId);
            return true;
        }

        return false;
    }

    bool CastUltimate(FPieceRuntime& piece, FBattleSystem& battleSystem)
    {
        if (!piece.def.isHero || !piece.alive || piece.onBench || piece.ultimateUsed || piece.currentMana < piece.def.maxMana)
        {
            return false;
        }

        if (piece.def.skillUltimate == "blue_surge")
        {
            int hitCount = 0;
            PlaySkillCastSfx();
            for (auto& candidate : battleSystem.AccessPieces())
            {
                if (!candidate.alive || candidate.onBench || candidate.def.team == piece.def.team)
                {
                    continue;
                }

                const int distance = std::max(std::abs(piece.col - candidate.col), std::abs(piece.row - candidate.row));
                if (distance > 3)
                {
                    continue;
                }

                battleSystem.ApplyAbilityDamage(piece, candidate, BlueSurgeDamage, glm::vec4(0.18f, 0.52f, 1.0f, 1.0f), "blue_surge");
                ++hitCount;
            }

            piece.currentMana = 0;
            piece.ultimateUsed = true;
            battleSystem.TriggerUltimatePresentation(piece, piece.def.skillUltimateName, glm::vec4(0.18f, 0.52f, 1.0f, 1.0f));
            battleSystem.TriggerUltimateAreaLight(piece, glm::vec3(0.18f, 0.52f, 1.0f));
            battleSystem.PushSkillEffect(FSkillEffect{
                .type = ESkillEffectType::ExpandingRing,
                .center = piece.node ? piece.node->WorldTranslation() : piece.targetWorldPos,
                .color = glm::vec4(0.18f, 0.52f, 1.0f, 1.0f),
                .durationMs = BlueSurgeDurationMs,
                .remainingMs = BlueSurgeDurationMs,
                .maxRadiusCells = 3.0f,
            });
            spdlog::info("[KongLie3D] {} cast R blue_surge hit {}", piece.pieceId, hitCount);
            return true;
        }

        if (piece.def.skillUltimate == "sydney_fury")
        {
            piece.currentMana = 0;
            piece.ultimateUsed = true;
            piece.furyTimerMs = FuryDurationMs;
            PlaySkillCastSfx();
            battleSystem.TriggerUltimatePresentation(piece, piece.def.skillUltimateName, glm::vec4(1.0f, 0.55f, 0.18f, 1.0f));
            battleSystem.TriggerUltimateAreaLight(piece, glm::vec3(1.0f, 0.55f, 0.18f));
            spdlog::info("[KongLie3D] {} cast R sydney_fury", piece.pieceId);
            return true;
        }

        return false;
    }
}
