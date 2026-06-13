#include "PerceptionSystem.h"

#include "EmployeeSystem.h"
#include "GatheringSystem.h"
#include "ProductionSystem.h"

#include <cmath>

namespace StudioSim
{
    namespace
    {
        constexpr double kScanIntervalSeconds = 0.5;
        constexpr double kStallThresholdMinutes = 60.0;
        constexpr double kStallMeetingCooldownMinutes = 120.0;
    }

    void PerceptionSystem::Reset()
    {
        accumulator_ = 0.0;
        lastProgress_ = 0.0;
        lastProgressGameMinutes_ = 0.0;
        lastStallMeetingGameMinutes_ = -1e9;
        observedEventCount_ = 0;
        trackingProduction_ = false;
    }

    void PerceptionSystem::Tick(double deltaRealSeconds, FWorldState& world,
                                std::vector<FEmployee>& employees,
                                const ProductionSystem& production, GatheringSystem& gathering)
    {
        if (world.todaysEvents.size() > observedEventCount_)
        {
            observedEventCount_ = world.todaysEvents.size();
            for (FEmployee& employee : employees)
            {
                employee.overrideTargetPoi.clear();
                employee.eventReactionPending = true;
                employee.nextDecisionAt = world.gameClockMinutes;
            }
        }

        accumulator_ += deltaRealSeconds;
        if (accumulator_ < kScanIntervalSeconds)
        {
            return;
        }
        accumulator_ = 0.0;

        if (!production.Active())
        {
            lastProgress_ = 0.0;
            lastProgressGameMinutes_ = world.gameClockMinutes;
            trackingProduction_ = false;
            return;
        }

        const double progress = production.State().overallProgress;
        if (!trackingProduction_)
        {
            trackingProduction_ = true;
            lastProgress_ = progress;
            lastProgressGameMinutes_ = world.gameClockMinutes;
            return;
        }
        if (std::abs(progress - lastProgress_) > 0.0001)
        {
            lastProgress_ = progress;
            lastProgressGameMinutes_ = world.gameClockMinutes;
            return;
        }

        const bool stalled = world.gameClockMinutes - lastProgressGameMinutes_ >= kStallThresholdMinutes;
        const bool meetingCooledDown =
            world.gameClockMinutes - lastStallMeetingGameMinutes_ >= kStallMeetingCooldownMinutes;
        if (stalled && meetingCooledDown && !gathering.HasActiveMeeting())
        {
            gathering.RequestMeeting("项目长时间没有进展，重新检查分工与当前短板");
            lastStallMeetingGameMinutes_ = world.gameClockMinutes;
        }
    }
}
