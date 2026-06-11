#pragma once

#include "AirportSimTypes.h"

#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace AirportSim
{
    class AgentSystem;
    class AirportMap;
    class FlightBoard;
    class QueueSystem;
    class TimeSystem;
    struct FAgent;

    // Layer 0 确定性执行层：旅客旅程状态机 + 员工日程状态机（§5.1/§5.2）。
    // 保证"机场永远在正确运转"；LLM 只通过 ApplyAirsideChoice 覆盖空侧弹性活动。
    class JourneySystem
    {
    public:
        void Reset(unsigned seed);
        void Tick(double gameMinutes, double dayMinutes, int dayIndex, AgentSystem& agents, AirportMap& map,
                  QueueSystem& queues, FlightBoard& flights, const TimeSystem& time);

        // Layer 1 接口：给空侧旅客指定下一个活动 POI（已通过白名单校验）。
        void ApplyAirsideChoice(FAgent& agent, const std::string& poiName, AgentSystem& agents, AirportMap& map);
        // 空侧弹性活动的合法 POI 类别（决策白名单，§5.3）。
        static bool IsAirsideLeisureCategory(const std::string& category);

    private:
        void TickStaff(double gameMinutes, double dayMinutes, AgentSystem& agents, AirportMap& map,
                       QueueSystem& queues, FlightBoard& flights, const TimeSystem& time);
        void TickPassengers(double gameMinutes, double dayMinutes, AgentSystem& agents, AirportMap& map,
                            QueueSystem& queues, FlightBoard& flights);
        void SpawnPassengers(double dayMinutes, AgentSystem& agents, FlightBoard& flights);
        void UpdateQueueStaffing(AgentSystem& agents, QueueSystem& queues);
        void EnsureQueues(AirportMap& map, QueueSystem& queues);
        void StartAirsideActivity(FAgent& agent, AgentSystem& agents, AirportMap& map, double gameMinutes);
        void ApplyAirsideChoiceSingle(FAgent& agent, const FPointOfInterest& poi, AgentSystem& agents,
                                      AirportMap& map);
        void ReleaseClaims(FAgent& agent, AirportMap& map, QueueSystem& queues);
        void DespawnPassenger(FAgent& agent, AgentSystem& agents, AirportMap& map, QueueSystem& queues);
        const FPointOfInterest* NearestOfCategory(const AirportMap& map, const std::string& category,
                                                  const glm::vec3& pos) const;
        std::string PreferredGroupQueue(const FAgent& agent, const std::string& prefix,
                                        const AgentSystem& agents, const QueueSystem& queues) const;
        glm::vec3 GroupTargetPosition(const FAgent& agent, const FPointOfInterest& poi, float frontOffset) const;
        double DynamicKioskServiceMinutes(const FAgent& agent);
        double Rand(double lo, double hi);

        std::mt19937 rng_{2024};
        bool queuesReady_ = false;
        int lastDay_ = -1;
        int passengerNameCursor_ = 0;
        int nextPassengerGroupId_ = 1;
        int lastFlowDiagnosticBucket_ = -1;
    };
}
