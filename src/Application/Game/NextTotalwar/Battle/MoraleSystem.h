#pragma once

#include "Battle/BattleState.h"

namespace NextTotalwar
{
    struct FMoraleTuning
    {
        float steadyThreshold = 48.0f;
        float routThreshold = 24.0f;
        float rallyThreshold = 38.0f;
        float routMinimumSeconds = 5.0f;
        float casualtyShock = 1.1f;
        float suppressionDrain = 3.0f;
        float flankDrain = 2.2f;
        float localDisadvantageDrain = 1.5f;
        float friendlyRoutDrain = 1.2f;
        float calmRecovery = 1.0f;
        float routSpeed = 10.5f;
        float leaveBattleExtent = 194.0f;
    };

    class FMoraleSystem
    {
    public:
        void Reset();
        void Tick(float deltaSeconds, std::vector<FRegiment>& regiments,
                  FBattleState& battleState, const FMoraleTuning& tuning = {});
        [[nodiscard]] int RoutedCount() const { return routedCount_; }
        [[nodiscard]] int RalliedCount() const { return ralliedCount_; }

    private:
        int routedCount_ = 0;
        int ralliedCount_ = 0;
    };
}
