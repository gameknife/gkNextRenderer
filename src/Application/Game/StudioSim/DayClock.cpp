#include "DayClock.h"

namespace StudioSim
{
    void DayClock::Reset(FWorldState& world, bool agentValidation) const
    {
        world = FWorldState{};
        world.phase = EDayPhase::Briefing;
        world.gameClockMinutes = WorkdayStartMinutes();
        if (agentValidation)
        {
            world.timeScale = 240.0f;
        }
    }

    void DayClock::BeginWorking(FWorldState& world) const
    {
        world.phase = EDayPhase::Working;
    }

    bool DayClock::TickWorking(FWorldState& world, double deltaRealSeconds, bool blocked) const
    {
        if (world.phase != EDayPhase::Working || world.paused || blocked)
        {
            return false;
        }
        world.gameClockMinutes += deltaRealSeconds * world.timeScale;
        if (world.gameClockMinutes < WorkdayEndMinutes())
        {
            return false;
        }
        world.gameClockMinutes = WorkdayEndMinutes();
        world.phase = EDayPhase::Review;
        return true;
    }

    void DayClock::BeginNextDay(FWorldState& world) const
    {
        const int nextDay = world.dayIndex + 1;
        const float timeScale = world.timeScale;
        world = FWorldState{};
        world.dayIndex = nextDay;
        world.timeScale = timeScale;
        world.gameClockMinutes = WorkdayStartMinutes();
        world.phase = EDayPhase::Briefing;
    }
}
