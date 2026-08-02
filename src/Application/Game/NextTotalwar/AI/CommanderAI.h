#pragma once

#include "Battle/BattleOrderSystem.h"
#include "Battle/BattleState.h"

namespace NextTotalwar
{
    enum class ECommanderPhase : uint8_t
    {
        Deploy,
        Advance,
        Engage,
        Press,
        Recover,
    };

    class FCommanderAI
    {
    public:
        void Reset(uint64_t seed, int faction);
        std::vector<FBattleOrder> Tick(float deltaSeconds, uint64_t combatTick,
                                       float battleSeconds,
                                       const std::vector<FRegiment>& regiments);

        [[nodiscard]] ECommanderPhase Phase() const { return phase_; }
        [[nodiscard]] uint64_t DecisionCount() const { return decisionCount_; }

    private:
        int FindClosestEnemy(const FRegiment& regiment,
                             const std::vector<FRegiment>& regiments) const;

        int faction_ = 1;
        float decisionCooldown_ = 0.0f;
        ECommanderPhase phase_ = ECommanderPhase::Deploy;
        uint64_t decisionCount_ = 0;
        FDeterministicRng rng_{991};
    };
}
