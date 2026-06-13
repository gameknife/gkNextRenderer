#pragma once

#include "StudioSimTypes.h"

namespace StudioSim
{
    class DayClock
    {
    public:
        void Reset(FWorldState& world, bool agentValidation) const;
        void BeginWorking(FWorldState& world) const;
        bool TickWorking(FWorldState& world, double deltaRealSeconds, bool blocked) const;
        void BeginNextDay(FWorldState& world) const;

        static constexpr double WorkdayStartMinutes() { return 9.0 * 60.0; }
        static constexpr double WorkdayEndMinutes() { return 18.0 * 60.0; }
    };
}
