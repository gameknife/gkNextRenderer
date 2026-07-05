#pragma once

#include <cstddef>
#include <vector>

namespace StudioSim
{
    class GatheringSystem;
    class ProductionSystem;
    struct FWorldState;
    struct FEmployee;

    class PerceptionSystem
    {
    public:
        void Reset();
        void Tick(double deltaRealSeconds, FWorldState& world, std::vector<FEmployee>& employees,
                  const ProductionSystem& production, GatheringSystem& gathering);

    private:
        double accumulator_ = 0.0;
        double lastProgress_ = 0.0;
        double lastProgressGameMinutes_ = 0.0;
        double lastStallMeetingGameMinutes_ = -1e9;
        size_t observedEventCount_ = 0;
        bool trackingProduction_ = false;
    };
}
