#pragma once

#include "NextTotalwarTypes.h"

#include <cstdint>
#include <vector>

namespace NextTotalwar
{
    enum class EBattlePhase : uint8_t
    {
        Loading,
        Briefing,
        Deployment,
        Active,
        Paused,
        Finished,
    };

    enum class EBattleResult : uint8_t
    {
        None,
        Victory,
        Defeat,
        Draw,
    };

    class FBattleSession
    {
    public:
        void Reset(uint64_t seed, bool skipBriefing = false);
        bool BeginDeployment();
        bool BeginBattle();
        bool TogglePause();
        void SetTimeScale(float timeScale);
        void Tick(float deltaSeconds, const std::vector<FRegiment>& regiments,
                  int playerFaction = 0);

        [[nodiscard]] EBattlePhase Phase() const { return phase_; }
        [[nodiscard]] EBattleResult Result() const { return result_; }
        [[nodiscard]] uint64_t Seed() const { return seed_; }
        [[nodiscard]] uint32_t Generation() const { return generation_; }
        [[nodiscard]] float BattleSeconds() const { return battleSeconds_; }
        [[nodiscard]] float TimeScale() const { return timeScale_; }
        [[nodiscard]] bool IsSimulationRunning() const { return phase_ == EBattlePhase::Active; }

        static const char* PhaseName(EBattlePhase phase);
        static const char* ResultName(EBattleResult result);

    private:
        EBattlePhase phase_ = EBattlePhase::Loading;
        EBattlePhase phaseBeforePause_ = EBattlePhase::Active;
        EBattleResult result_ = EBattleResult::None;
        uint64_t seed_ = 1337;
        uint32_t generation_ = 0;
        float battleSeconds_ = 0.0f;
        float timeScale_ = 1.0f;
    };
}
