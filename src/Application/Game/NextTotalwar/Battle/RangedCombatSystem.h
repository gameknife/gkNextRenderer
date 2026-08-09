#pragma once

#include "Battle/BattleState.h"

namespace NextTotalwar
{
    struct FRangedCombatTuning
    {
        int arrowsPerVolleyDivisor = 5;
        float distanceAccuracyPenalty = 0.45f;
        float suppressionPerHit = 0.035f;
        float deathClipSeconds = 0.8f;
    };

    class FRangedCombatSystem
    {
    public:
        void Reset(uint64_t seed);
        void Tick(float deltaSeconds, std::vector<FRegiment>& regiments,
                  FBattleState& battleState, const FRangedCombatTuning& tuning = {},
                  const FBattleContext* context = nullptr);
        [[nodiscard]] uint64_t VolleyCount() const { return volleyCount_; }
        [[nodiscard]] uint64_t CasualtyCount() const { return casualtyCount_; }

    private:
        int FindTarget(const FRegiment& attacker, const std::vector<FRegiment>& regiments) const;
        int PickAliveSoldier(FRegiment& regiment);
        bool HasLineOfFire(const FRegiment& attacker, const FRegiment& target,
                           const FBattleContext* context) const;

        FDeterministicRng rng_{1337};
        uint64_t volleyCount_ = 0;
        uint64_t casualtyCount_ = 0;
    };
}
