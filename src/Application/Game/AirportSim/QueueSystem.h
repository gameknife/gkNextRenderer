#pragma once

#include "AirportSimTypes.h"

#include <random>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace AirportSim
{
    class AgentSystem;

    // 服务点队列：slot 链生成/占用/推进 + 队首服务计时。
    // 队列从服务点沿 queueDir（POI front 方向）向外延伸。
    class QueueSystem
    {
    public:
        struct FQueue
        {
            std::string id;             // = POI 名（checkin_01 / security_01 / gate_01）
            glm::vec3   servicePoint{0.0f};
            glm::vec3   queueDir{0.0f, 0.0f, 1.0f};
            double      serviceMin = 5.0;
            double      serviceMax = 10.0;
            bool        staffed = false;    // 无员工在岗时队列不推进
            std::vector<int> agents;        // 队首在前
            bool        serving = false;
            double      serviceStartedAt = 0.0;
            double      serviceEndAt = 0.0;
            double      currentServiceMinutes = 0.0;
        };

        void Reset(unsigned seed);
        void EnsureQueue(const std::string& id, const glm::vec3& servicePoint, const glm::vec3& queueDir,
                         double serviceMin, double serviceMax);
        void SetStaffed(const std::string& id, bool staffed);

        // 入队，返回 slot 下标（0 = 队首）；已在队中返回当前下标。
        int Join(const std::string& id, int agentId);
        void Leave(const std::string& id, int agentId);
        // agent 在该队列中的当前 slot（-1 = 不在队中）。
        int SlotOf(const std::string& id, int agentId) const;
        glm::vec3 SlotPosition(const std::string& id, int slot) const;
        int Length(const std::string& id) const;
        bool IsStaffed(const std::string& id) const;
        // 该前缀下最短的有人值守队列（无人值守时退而求其次返回最短队列）；空串 = 无队列。
        std::string ShortestQueue(const std::string& prefix) const;

        // 推进服务计时；服务完成的队首 agent id 进入完成列表（消费即清空）。
        void Tick(double gameMinutes, const AgentSystem& agents);
        std::vector<int> ConsumeCompleted();

        const std::vector<FQueue>& Queues() const { return queues_; }

    private:
        FQueue* Find(const std::string& id);
        const FQueue* Find(const std::string& id) const;

        std::vector<FQueue> queues_;
        std::vector<int> completed_;
        std::mt19937 rng_{777};
    };
}
