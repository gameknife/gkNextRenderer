#pragma once

#include "Common/CoreMinimal.hpp"
#include "KongLie3DPiece.hpp"

namespace KongLie3D
{
    enum class EProjectileKind : uint8_t
    {
        AttackAD,
        AttackAP,
        Heal,
    };

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

    struct FProjectile
    {
        glm::vec3 startPos = glm::vec3(0.0f);
        glm::vec3 endPos = glm::vec3(0.0f);
        float durationMs = 0.0f;
        float elapsedMs = 0.0f;
        glm::vec3 color = glm::vec3(1.0f);
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        std::shared_ptr<Assets::Node> node;
        std::function<void()> onHit;
        size_t poolIndex = std::numeric_limits<size_t>::max();
    };

    struct FProjectilePoolEntry
    {
        EProjectileKind kind = EProjectileKind::AttackAD;
        uint32_t modelId = 0;
        uint32_t materialId = 0;
        std::shared_ptr<Assets::Node> node;
        bool active = false;
    };

    struct FImpactDebris
    {
        std::shared_ptr<Assets::Node> node;
        glm::vec3 startPos = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        float startScale = 1.0f;
        float durationMs = 0.0f;
        float elapsedMs = 0.0f;
        size_t poolIndex = std::numeric_limits<size_t>::max();
    };

    struct FImpactDebrisPoolEntry
    {
        std::shared_ptr<Assets::Node> node;
        bool active = false;
    };

    struct FUltimatePresentation
    {
        glm::vec3 cameraFocusPos = glm::vec3(0.0f);
        glm::vec4 flashColor = glm::vec4(1.0f);
        glm::vec4 titleColor = glm::vec4(1.0f);
        std::string title;
        float flashRemainingMs = 0.0f;
        float flashDurationMs = 0.0f;
        float cameraFocusRemainingMs = 0.0f;
        float cameraFocusDurationMs = 0.0f;
        float titleRemainingMs = 0.0f;
        float titleDurationMs = 0.0f;
    };

    struct FSynergyStatus
    {
        std::string id;
        std::string name;
        int count = 0;
        int activeCount = 0;
        FSynergyTier activeTier;
        int nextTierCount = 0;
        bool active = false;
    };

    class FBattleSystem
    {
    public:
        void BindPieces(std::vector<FPieceRuntime>* pieces);
        void ConfigureVisualResources(uint32_t hitFlashMaterialId,
                                      std::vector<FProjectilePoolEntry> projectilePool,
                                      std::vector<FImpactDebrisPoolEntry> debrisPool);
        void SetRelics(std::vector<FRelicDef> relics);
        void SetSynergies(std::vector<FSynergyDef> synergies);
        void SetEnemyDamageMultiplier(float enemyDamageMultiplier);
        void Reset();
        void Start();
        void Update(double deltaSeconds);
        bool ConsumeSceneDirty();
        void TogglePause();
        void SetSpeedMultiplier(float speedMultiplier);
        void CycleSpeedMultiplier();
        void RequestUltimate(const std::string& heroId);
        void SelectRelic(const std::string& relicId);
        void TriggerUltimatePresentation(const FPieceRuntime& caster, std::string title, const glm::vec4& color);
        void TriggerUltimateAreaLight(const FPieceRuntime& caster, const glm::vec3& color);
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
        float GetScreenShakeMs() const { return screenShakeMs_; }
        float GetSpeedMultiplier() const { return std::clamp(speedMultiplier_, 1.0f, 4.0f); }
        float* GetSpeedMultiplierCVarPtr() { return &speedMultiplier_; }
        const std::vector<FAttackTrace>& GetAttackTraces() const { return attackTraces_; }
        const std::vector<FSkillEffect>& GetSkillEffects() const { return skillEffects_; }
        const std::vector<FDamagePopup>& GetDamagePopups() const { return damagePopups_; }
        const std::vector<FRelicDef>& GetRelics() const { return relics_; }
        const FUltimatePresentation& GetUltimatePresentation() const { return ultimatePresentation_; }
        const std::vector<FSynergyStatus>& GetActiveSynergies() const { return activeSynergies_; }
        std::vector<FSynergyStatus> BuildSynergyPreview() const;
        const FRelicDef* GetSelectedRelic() const;

    private:
        void ApplySelectedRelic();
        std::vector<FSynergyStatus> EvaluateSynergiesFromDeployment() const;
        void ApplySynergyBonuses(const std::vector<FSynergyStatus>& activeSynergies);
        static void ApplyTierToPiece(FPieceRuntime& piece, const FSynergyStatus& synergyStatus);
        void Tick();
        void UpdateMovementInterpolation(float deltaMs);
        void UpdateDeathAnimations(float deltaMs);
        void UpdateAttackTraces(float deltaMs);
        void UpdateSkillEffects(float deltaMs);
        void UpdateDamagePopups(float deltaMs);
        void UpdateProjectiles(float deltaMs);
        void UpdateImpactDebris(float deltaMs);
        void UpdateHitFlashes(float deltaMs);
        void UpdateUltimatePresentation(float deltaMs);
        void UpdateTempLights(float deltaMs);
        uint32_t EnsureTempLightMaterial(const glm::vec3& color);
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
        void TriggerHitFeedback(FPieceRuntime& target, const glm::vec3& impactPos);
        bool SpawnProjectile(EProjectileKind kind,
                             const glm::vec3& startPos,
                             const glm::vec3& endPos,
                             float durationMs,
                             const glm::vec3& color,
                             std::function<void()> onHit);
        void SpawnImpactDebris(const glm::vec3& impactPos, int count = 3, float startScale = 1.0f, float speedScale = 1.0f);

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
        std::vector<FSynergyDef> synergyDefs_;
        std::vector<FSynergyStatus> activeSynergies_;
        int selectedRelicIndex_ = -1;
        uint32_t hitFlashMaterialId_ = 0;
        float screenShakeMs_ = 0.0f;
        float speedMultiplier_ = 1.0f;
        float enemyDamageMultiplier_ = 1.0f;
        std::vector<FProjectilePoolEntry> projectilePool_;
        std::vector<FProjectile> projectiles_;
        std::vector<FImpactDebrisPoolEntry> debrisPool_;
        std::vector<FImpactDebris> impactDebris_;
        FUltimatePresentation ultimatePresentation_;
        struct FTempLight
        {
            int lightIndex = -1;
            float remainingMs = 0.0f;
        };
        std::vector<FTempLight> tempLights_;
        uint32_t blueUltimateLightMaterialId_ = std::numeric_limits<uint32_t>::max();
        uint32_t orangeUltimateLightMaterialId_ = std::numeric_limits<uint32_t>::max();
        bool sceneDirty_ = false;
    };
}
