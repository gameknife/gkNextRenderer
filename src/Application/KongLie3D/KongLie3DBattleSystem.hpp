#pragma once

#include "Common/CoreMinimal.hpp"
#include "KongLie3DPiece.hpp"

namespace KongLie3D
{
    enum class EBattleState : uint8_t
    {
        Deployment,
        Battle,
        Ended,
    };

    struct FAttackTrace
    {
        glm::vec3 from = glm::vec3(0.0f);
        glm::vec3 to = glm::vec3(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        float remainingMs = 0.0f;
    };

    enum class ESkillEffectType : uint8_t
    {
        Beam,
        ExpandingRing,
    };

    struct FSkillEffect
    {
        ESkillEffectType type = ESkillEffectType::Beam;
        glm::vec3 from = glm::vec3(0.0f);
        glm::vec3 to = glm::vec3(0.0f);
        glm::vec3 center = glm::vec3(0.0f);
        glm::vec4 color = glm::vec4(1.0f);
        float durationMs = 0.0f;
        float remainingMs = 0.0f;
        float maxRadiusCells = 0.0f;
    };

    struct FDamagePopup
    {
        glm::vec3 worldPos = glm::vec3(0.0f);
        std::string text;
        glm::vec4 color = glm::vec4(1.0f);
        float durationMs = 0.0f;
        float remainingMs = 0.0f;
    };

    class FBattleSystem
    {
    public:
        void BindPieces(std::vector<FPieceRuntime>* pieces);
        void SetRelics(std::vector<FRelicDef> relics);
        void Reset();
        void Start();
        void Update(double deltaSeconds);
        bool ConsumeSceneDirty();
        void TogglePause();
        void RequestUltimate(const std::string& heroId);
        void SelectRelic(const std::string& relicId);
        std::vector<FPieceRuntime>& AccessPieces() { return *pieces_; }
        const std::vector<FPieceRuntime>& AccessPieces() const { return *pieces_; }
        FPieceRuntime* FindNearestEnemyForSkill(FPieceRuntime& piece);
        FPieceRuntime* FindFarthestEnemyForSkill(FPieceRuntime& piece);
        FPieceRuntime* FindNearestAllyForSkill(FPieceRuntime& piece);
        bool FindAdjacentCellForSkill(const FPieceRuntime& mover, const FPieceRuntime& target, glm::ivec2& outCell) const;
        void TeleportPiece(FPieceRuntime& piece, int col, int row);
        void ApplyAbilityDamage(FPieceRuntime& attacker, FPieceRuntime& target, int damage, const glm::vec4& color,
                                const char* sourceTag);
        void PushSkillEffect(const FSkillEffect& effect);

        EBattleState GetState() const { return state_; }
        bool IsPaused() const { return paused_; }
        const std::string& GetWinnerTeam() const { return winnerTeam_; }
        float GetElapsedMs() const { return elapsedMs_; }
        bool IsOvertimeActive() const { return overtimeActive_; }
        float GetOvertimeStartMs() const { return overtimeStartMs_; }
        float GetDamageMultiplier() const { return dmgMult_; }
        float GetHealMultiplier() const { return healMult_; }
        const std::vector<FAttackTrace>& GetAttackTraces() const { return attackTraces_; }
        const std::vector<FSkillEffect>& GetSkillEffects() const { return skillEffects_; }
        const std::vector<FDamagePopup>& GetDamagePopups() const { return damagePopups_; }
        const std::vector<FRelicDef>& GetRelics() const { return relics_; }
        const FRelicDef* GetSelectedRelic() const;

    private:
        void ApplySelectedRelic();
        void Tick();
        void UpdateMovementInterpolation(float deltaMs);
        void UpdateDeathAnimations(float deltaMs);
        void UpdateAttackTraces(float deltaMs);
        void UpdateSkillEffects(float deltaMs);
        void UpdateDamagePopups(float deltaMs);
        void SnapAllPiecesToTargets();
        FPieceRuntime* FindNearestEnemy(FPieceRuntime& piece);
        FPieceRuntime* FindLowestHealthAlly(FPieceRuntime& piece);
        void Attack(FPieceRuntime& attacker, FPieceRuntime& target);
        bool TryHeal(FPieceRuntime& healer);
        void StartMoveTowards(FPieceRuntime& piece, const FPieceRuntime& target);
        void KillPiece(FPieceRuntime& piece);
        void RecordAttackTrace(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
        void RecordDamagePopup(const glm::vec3& worldPos, const std::string& text, const glm::vec4& color);
        FPieceRuntime* FindPieceById(const std::string& pieceId);
        bool IsUltimateReady(const FPieceRuntime& piece) const;
        void ProcessPendingUltimates();

        std::vector<FPieceRuntime>* pieces_ = nullptr;
        EBattleState state_ = EBattleState::Deployment;
        bool paused_ = false;
        float tickAccumulatorMs_ = 0.0f;
        float elapsedMs_ = 0.0f;
        float dmgMult_ = 1.0f;
        float healMult_ = 1.0f;
        bool overtimeActive_ = false;
        float overtimeStartMs_ = 0.0f;
        std::string winnerTeam_;
        std::vector<FAttackTrace> attackTraces_;
        std::vector<FSkillEffect> skillEffects_;
        std::vector<FDamagePopup> damagePopups_;
        std::vector<std::string> pendingUltimateIds_;
        std::vector<FRelicDef> relics_;
        int selectedRelicIndex_ = -1;
        bool sceneDirty_ = false;
    };
}
