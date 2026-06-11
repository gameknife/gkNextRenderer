#pragma once

#include "AirportSimTypes.h"

#include <random>
#include <vector>

namespace AirportSim
{
    // 当日离港航班表 + 状态机 + 状态变化事件（§4.3）。
    class FlightBoard
    {
    public:
        struct FFlightEvent
        {
            int flightIdx = -1;
            EFlightState newState = EFlightState::Scheduled;
        };

        void Reset(unsigned seed);
        // 每天 05:00 重新生成；推进各航班状态机并产出事件。
        void Tick(int dayIndex, double dayMinutes);

        const std::vector<FFlight>& Flights() const { return flights_; }
        std::vector<FFlight>& FlightsMutable() { return flights_; }
        // 本帧状态变化事件（消费即清空）。
        std::vector<FFlightEvent> ConsumeEvents();

    private:
        void GenerateDay(int dayIndex);

        std::vector<FFlight> flights_;
        std::vector<FFlightEvent> events_;
        std::mt19937 rng_{12345};
        int generatedDay_ = -1;
    };
}
