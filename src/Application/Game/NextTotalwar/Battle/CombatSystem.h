#pragma once

#include "Battle/BattleState.h"
#include "Battle/CombatGrid.h"
#include "NextTotalwarCombatConfig.hpp"

namespace NextTotalwar
{
    class FCombatSystem
    {
    public:
        explicit FCombatSystem(uint64_t seed = 1337, float worldHalfExtent = 200.0f);

        void Reset(uint64_t seed);
        void Tick(float deltaSeconds, std::vector<FRegiment>& regiments,
                  const FCombatTuning& tuning, FBattleState& battleState);

    private:
        void UpdateEngagements(std::vector<FRegiment>& regiments, const FCombatTuning& tuning);
        void PairSoldiers(std::vector<FRegiment>& regiments, const FCombatTuning& tuning);
        void ResolveAttacks(float deltaSeconds, std::vector<FRegiment>& regiments,
                            const FCombatTuning& tuning, FBattleState& battleState);
        bool IsValidTarget(const std::vector<FRegiment>& regiments, size_t attackerRegiment,
                           const FSoldier& attacker, float maxDistance) const;
        size_t FlatIndex(size_t regiment, size_t soldier) const;

        FCombatGrid grid_;
        FDeterministicRng rng_;
        std::vector<size_t> regimentOffsets_;
        std::vector<uint8_t> attackerCounts_;
        std::vector<FCombatGridEntry> queryResults_;
    };
}
