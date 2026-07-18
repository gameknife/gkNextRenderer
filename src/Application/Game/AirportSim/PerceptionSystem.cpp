#include "PerceptionSystem.h"

#include "AgentSystem.h"
#include "AirportSimConfig.hpp"
#include "FlightBoard.h"
#include "QueueSystem.h"

#include <fmt/format.h>

#include <glm/glm.hpp>

namespace AirportSim
{
    namespace
    {
        constexpr double kGreetCooldownMinutes = 45.0; // 同一 agent 两次寒暄触发的最小间隔

        bool CanReceiveEvent(const FAgent& agent)
        {
            return agent.eventNote.empty() && !agent.decisionPending;
        }
    }

    void PerceptionSystem::Reset()
    {
        accumulator_ = 0.0;
    }

    void PerceptionSystem::Tick(double deltaRealSeconds, double gameMinutes, AgentSystem& agents,
                                const QueueSystem& queues, FlightBoard& flights)
    {
        accumulator_ += deltaRealSeconds;
        const bool scan = accumulator_ >= Config::kPerceptionIntervalSeconds;
        if (scan)
        {
            accumulator_ = 0.0;
        }

        // 航班广播 → 该航班空侧旅客的决策时刻（"你的航班开始登机了"）。
        for (const auto& event : flights.ConsumeEvents())
        {
            if (event.newState != EFlightState::Boarding && event.newState != EFlightState::Final)
            {
                continue;
            }
            const FFlight& flight = flights.Flights()[static_cast<size_t>(event.flightIdx)];
            for (auto& agent : agents.Agents())
            {
                if (agent.active && agent.role == EAgentRole::Passenger && agent.flightIdx == event.flightIdx &&
                    CanReceiveEvent(agent))
                {
                    agent.eventNote = event.newState == EFlightState::Boarding
                                          ? fmt::format("你的航班 {} 开始登机了", flight.number)
                                          : fmt::format("你的航班 {} 最后召集！", flight.number);
                    agent.nextDecisionAt = gameMinutes;
                }
            }
        }

        if (!scan)
        {
            return;
        }

        auto& all = agents.Agents();

        // 同行讨论：领队定期和附近成员确认行程，给 Layer 1 一个共同决策时刻。
        for (auto& leader : all)
        {
            if (!leader.active || !leader.IsGroupLeader() || !CanReceiveEvent(leader) ||
                gameMinutes - leader.lastGroupDiscussionAt < Config::kGroupDiscussionCooldownMinutes)
            {
                continue;
            }

            FAgent* companion = nullptr;
            for (auto& other : all)
            {
                if (!other.active || other.groupId != leader.groupId || other.id == leader.id)
                {
                    continue;
                }
                const glm::vec3 d = leader.position - other.position;
                if (glm::length(glm::vec2(d.x, d.z)) <= Config::kNeighborRadius * 1.5f)
                {
                    companion = &other;
                    break;
                }
            }
            if (companion == nullptr)
            {
                continue;
            }

            const bool inJourneyDiscussion =
                !leader.queueId.empty() || leader.pstate == EPassengerState::AirsideIdle ||
                leader.pstate == EPassengerState::AirsideUse;
            if (!inJourneyDiscussion)
            {
                continue;
            }

            leader.eventNote = fmt::format("和同行的{}讨论接下来的安排", companion->name);
            leader.nextDecisionAt = gameMinutes;
            leader.lastGroupDiscussionAt = gameMinutes;
            companion->lastGroupDiscussionAt = gameMinutes;
        }

        // 相遇寒暄：3m 内邻居，带冷却。
        for (size_t i = 0; i < all.size(); ++i)
        {
            FAgent& a = all[i];
            if (!a.active || gameMinutes - a.lastGreetAt < kGreetCooldownMinutes || !CanReceiveEvent(a))
            {
                continue;
            }
            for (size_t j = i + 1; j < all.size(); ++j)
            {
                FAgent& b = all[j];
                if (!b.active)
                {
                    continue;
                }
                if (a.IsGroupedPassenger() && b.groupId == a.groupId)
                {
                    continue;
                }
                const glm::vec3 d = a.position - b.position;
                if (glm::length(glm::vec2(d.x, d.z)) > Config::kNeighborRadius)
                {
                    continue;
                }
                // 同事相遇 / 同店共处更有戏；旅客排队中不寒暄（有专属抱怨事件）。
                const bool bothStaff = a.role != EAgentRole::Passenger && b.role != EAgentRole::Passenger;
                const bool airsidePair = a.pstate == EPassengerState::AirsideUse &&
                                         b.pstate == EPassengerState::AirsideUse && a.targetPoi == b.targetPoi;
                if (bothStaff || airsidePair)
                {
                    a.eventNote = fmt::format("遇到了{}（{}）", b.name, RoleLabelZh(b.role));
                    a.nextDecisionAt = gameMinutes;
                    a.lastGreetAt = gameMinutes;
                    b.lastGreetAt = gameMinutes; // 双方都冷却，避免来回刷
                    break;
                }
            }
        }

        // 到店事件：旅客在店里消费时，店员招呼 + 顾客发逛店感想（空侧人气的主要话头来源）。
        for (auto& customer : all)
        {
            if (!customer.active || customer.role != EAgentRole::Passenger ||
                customer.pstate != EPassengerState::AirsideUse || customer.targetPoi.empty() ||
                !customer.seatPoi.empty()) // 坐着候机不算逛店
            {
                continue;
            }
            // 店员侧：我的岗位正好是这位顾客所在的 POI。
            for (auto& clerk : all)
            {
                if (clerk.active && clerk.role == EAgentRole::Clerk && clerk.sstate == EStaffState::OnDuty &&
                    clerk.postPoi == customer.targetPoi && CanReceiveEvent(clerk) &&
                    gameMinutes - clerk.lastGreetAt >= kGreetCooldownMinutes * 0.6)
                {
                    clerk.eventNote = fmt::format("顾客{}正在你店里（{}）挑东西，招呼一下", customer.name,
                                                  customer.targetPoi);
                    clerk.nextDecisionAt = gameMinutes;
                    clerk.lastGreetAt = gameMinutes;
                    break;
                }
            }
            // 顾客侧：偶尔对商品/旅途发一句感想。
            if (CanReceiveEvent(customer) && gameMinutes - customer.lastGreetAt >= kGreetCooldownMinutes)
            {
                customer.eventNote = fmt::format("你正在{}里挑东西", customer.targetPoi);
                customer.nextDecisionAt = gameMinutes;
                customer.lastGreetAt = gameMinutes;
            }
        }

        // 长队抱怨：队列 >6 人时队尾旅客不耐烦。
        for (const auto& q : queues.Queues())
        {
            if (static_cast<int>(q.agents.size()) <= Config::kLongQueueThreshold)
            {
                continue;
            }
            for (size_t k = q.agents.size() - 2; k < q.agents.size(); ++k)
            {
                if (FAgent* tail = agents.FindById(q.agents[k]))
                {
                    if (CanReceiveEvent(*tail) && tail->mood != EMood::Annoyed)
                    {
                        tail->eventNote = fmt::format("排在 {} 的长队里，前面还有 {} 人", q.id, k);
                        tail->nextDecisionAt = gameMinutes;
                    }
                }
            }
        }
    }
}
