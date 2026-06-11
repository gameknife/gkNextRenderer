#include "JourneySystem.h"

#include "AgentSystem.h"
#include "AirportMap.h"
#include "AirportSimConfig.hpp"
#include "FlightBoard.h"
#include "QueueSystem.h"
#include "TimeSystem.h"

#include <algorithm>
#include <cmath>

#include <fmt/format.h>
#include <spdlog/spdlog.h>

namespace AirportSim
{
    namespace
    {
        // 员工在岗站位：柜台类站柜台后，面向顾客。
        glm::vec3 DutyStandPos(const FPointOfInterest& poi)
        {
            return AirportMap::ServicePoint(poi, Config::kCounterStandOffset);
        }

        float FaceCustomersYaw(const FPointOfInterest& poi)
        {
            return std::atan2(poi.frontDir.x, poi.frontDir.z);
        }

        // 保洁/保安巡逻点序列（缺失的点位运行时跳过）。
        const char* kCleanerPatrol[] = {"info_01", "checkin_03", "kiosk_02", "security_02", "cafe_01",
                                        "wait_03",  "wait_09",    "toilet_01", "gate_03",    "shop_01"};
        const char* kGuardPatrol[] = {"entrance_02", "checkin_05", "security_04", "food_01", "wait_06",
                                      "gate_01",     "gate_06",    "book_01",     "staff_01"};
    }

    void JourneySystem::Reset(unsigned seed)
    {
        rng_.seed(seed);
        queuesReady_ = false;
        lastDay_ = -1;
        passengerNameCursor_ = 0;
    }

    double JourneySystem::Rand(double lo, double hi)
    {
        std::uniform_real_distribution<double> dist(lo, hi);
        return dist(rng_);
    }

    bool JourneySystem::IsAirsideLeisureCategory(const std::string& category)
    {
        return category == "cafe" || category == "food" || category == "shop" || category == "book" ||
               category == "gift" || category == "vending" || category == "atm" || category == "toilet" ||
               category == "wait";
    }

    const FPointOfInterest* JourneySystem::NearestOfCategory(const AirportMap& map, const std::string& category,
                                                             const glm::vec3& pos) const
    {
        const FPointOfInterest* best = nullptr;
        float bestDist = 1e9f;
        for (const auto* poi : map.PointsOfCategory(category))
        {
            const float d = glm::distance(glm::vec2(poi->worldPos.x, poi->worldPos.z), glm::vec2(pos.x, pos.z));
            if (d < bestDist)
            {
                bestDist = d;
                best = poi;
            }
        }
        return best;
    }

    void JourneySystem::EnsureQueues(AirportMap& map, QueueSystem& queues)
    {
        if (queuesReady_)
        {
            return;
        }
        for (const auto& poi : map.Points())
        {
            if (poi.category == "checkin")
            {
                queues.EnsureQueue(poi.name, AirportMap::ServicePoint(poi, Config::kCheckinServiceOffset),
                                   poi.frontDir, Config::kCheckinServiceMin, Config::kCheckinServiceMax);
            }
            else if (poi.category == "security")
            {
                queues.EnsureQueue(poi.name, AirportMap::ServicePoint(poi, Config::kSecurityEntryOffset),
                                   poi.frontDir, Config::kSecurityServiceMin, Config::kSecurityServiceMax);
            }
            else if (poi.category == "gate")
            {
                queues.EnsureQueue(poi.name, AirportMap::ServicePoint(poi, Config::kGateServiceOffset),
                                   poi.frontDir, Config::kGateServiceMin, Config::kGateServiceMax);
            }
        }
        queuesReady_ = true;
        SPDLOG_INFO("AirportSim/Journey: queues initialized ({} total)", queues.Queues().size());
    }

    void JourneySystem::ReleaseClaims(FAgent& agent, AirportMap& map, QueueSystem& queues)
    {
        if (!agent.queueId.empty())
        {
            queues.Leave(agent.queueId, agent.id);
            agent.queueId.clear();
            agent.queuedSlot = -1;
        }
        if (!agent.targetPoi.empty())
        {
            map.Release(agent.targetPoi, agent.id);
            agent.targetPoi.clear();
        }
        if (!agent.seatPoi.empty())
        {
            map.ReleaseSeat(agent.seatPoi, agent.seatSlot, agent.id);
            agent.seatPoi.clear();
            agent.seatSlot = -1;
        }
    }

    void JourneySystem::DespawnPassenger(FAgent& agent, AgentSystem& agents, AirportMap& map, QueueSystem& queues)
    {
        ReleaseClaims(agent, map, queues);
        agents.Despawn(agent);
    }

    void JourneySystem::Tick(double gameMinutes, double dayMinutes, int dayIndex, AgentSystem& agents,
                             AirportMap& map, QueueSystem& queues, FlightBoard& flights, const TimeSystem& time)
    {
        EnsureQueues(map, queues);

        // 跨天：清掉昨日残留旅客（航班表已重生成，flightIdx 失效）。
        if (dayIndex != lastDay_)
        {
            if (lastDay_ >= 0)
            {
                for (auto& agent : agents.Agents())
                {
                    if (agent.active && agent.role == EAgentRole::Passenger)
                    {
                        DespawnPassenger(agent, agents, map, queues);
                    }
                }
            }
            lastDay_ = dayIndex;
        }

        TickStaff(gameMinutes, dayMinutes, agents, map, queues, flights, time);
        SpawnPassengers(dayMinutes, agents, flights);
        TickPassengers(gameMinutes, dayMinutes, agents, map, queues, flights);
        UpdateQueueStaffing(agents, queues);
    }

    // ---------------- 员工（§5.2） ----------------

    void JourneySystem::TickStaff(double gameMinutes, double dayMinutes, AgentSystem& agents, AirportMap& map,
                                  QueueSystem& queues, FlightBoard& flights, const TimeSystem& time)
    {
        // 班前 20 分钟从陆侧生成通勤。
        for (int i = 0; i < Config::kStaffCount; ++i)
        {
            const auto& def = Config::kStaffRoster[i];
            if (TimeSystem::IsCommuteWindow(def.shift, dayMinutes) || TimeSystem::IsShiftActiveAt(def.shift, dayMinutes))
            {
                FAgent* existing = nullptr;
                for (auto& agent : agents.Agents())
                {
                    if (agent.active && agent.rosterIdx == i)
                    {
                        existing = &agent;
                        break;
                    }
                }
                if (existing == nullptr)
                {
                    const glm::vec3 spawn = Config::kSpawnPoints[i % 3];
                    if (FAgent* staff = agents.SpawnStaff(i, spawn))
                    {
                        const FPointOfInterest* entrance = NearestOfCategory(map, "entrance", spawn);
                        if (entrance != nullptr)
                        {
                            agents.MoveTo(*staff, AirportMap::ServicePoint(*entrance, 0.0f));
                        }
                        SPDLOG_INFO("AirportSim/Staff: {} ({}) commuting, shift {}", staff->name,
                                    RoleLabelZh(staff->role), ShiftName(staff->shift));
                    }
                }
            }
        }

        // 登机口职员动态调度：Boarding 的航班抓一个空闲 GateAgent。
        for (auto& flight : flights.FlightsMutable())
        {
            if (flight.state != EFlightState::Boarding && flight.state != EFlightState::Final)
            {
                continue;
            }
            bool covered = false;
            for (const auto& agent : agents.Agents())
            {
                if (agent.active && agent.role == EAgentRole::GateAgent && agent.assignedGate == flight.gatePoi)
                {
                    covered = true;
                    break;
                }
            }
            if (covered)
            {
                continue;
            }
            for (auto& agent : agents.Agents())
            {
                if (agent.active && agent.role == EAgentRole::GateAgent && agent.assignedGate.empty() &&
                    (agent.sstate == EStaffState::OnDuty || agent.sstate == EStaffState::ToPost))
                {
                    agent.assignedGate = flight.gatePoi;
                    if (const FPointOfInterest* gate = map.FindByName(flight.gatePoi))
                    {
                        agents.MoveTo(agent, DutyStandPos(*gate));
                        agent.sstate = EStaffState::ToPost;
                    }
                    SPDLOG_INFO("AirportSim/Staff: {} -> {} for {}", agent.name, flight.gatePoi, flight.number);
                    break;
                }
            }
        }

        for (auto& agent : agents.Agents())
        {
            if (!agent.active || agent.role == EAgentRole::Passenger)
            {
                continue;
            }

            const bool shiftActive = TimeSystem::IsShiftActiveAt(agent.shift, dayMinutes);

            switch (agent.sstate)
            {
            case EStaffState::Commute:
                if (agents.Arrived(agent))
                {
                    // 进了 entrance：巡逻岗直接开巡，固定岗走向岗位。
                    if (agent.role == EAgentRole::Cleaner || agent.role == EAgentRole::Guard)
                    {
                        agent.sstate = EStaffState::Patrol;
                        agent.patrolIdx = 0;
                        agent.stateUntil = 0.0;
                    }
                    else
                    {
                        std::string post = agent.role == EAgentRole::GateAgent && !agent.assignedGate.empty()
                                               ? agent.assignedGate
                                               : agent.postPoi;
                        if (const FPointOfInterest* poi = map.FindByName(post))
                        {
                            agents.MoveTo(agent, DutyStandPos(*poi));
                        }
                        agent.sstate = EStaffState::ToPost;
                    }
                }
                break;

            case EStaffState::ToPost:
                if (agents.Arrived(agent))
                {
                    agent.sstate = EStaffState::OnDuty;
                    const std::string post = agent.role == EAgentRole::GateAgent && !agent.assignedGate.empty()
                                                 ? agent.assignedGate
                                                 : agent.postPoi;
                    if (const FPointOfInterest* poi = map.FindByName(post))
                    {
                        agent.yaw = FaceCustomersYaw(*poi);
                    }
                    agent.anim = EAgentAnimHint::Work;
                }
                break;

            case EStaffState::OnDuty:
            {
                agent.anim = EAgentAnimHint::Work;
                // GateAgent：登机结束回办公室。
                if (agent.role == EAgentRole::GateAgent && !agent.assignedGate.empty())
                {
                    bool stillActive = false;
                    for (const auto& flight : flights.Flights())
                    {
                        if (flight.gatePoi == agent.assignedGate &&
                            (flight.state == EFlightState::Boarding || flight.state == EFlightState::Final))
                        {
                            stillActive = true;
                            break;
                        }
                    }
                    if (!stillActive)
                    {
                        agent.assignedGate.clear();
                        if (const FPointOfInterest* office = map.FindByName(agent.postPoi))
                        {
                            agents.MoveTo(agent, DutyStandPos(*office));
                            agent.sstate = EStaffState::ToPost;
                        }
                    }
                }
                if (!shiftActive)
                {
                    agent.sstate = EStaffState::OffDuty;
                    const FPointOfInterest* entrance = NearestOfCategory(map, "entrance", agent.position);
                    agents.MoveTo(agent, entrance != nullptr ? AirportMap::ServicePoint(*entrance, 0.0f)
                                                             : Config::kSpawnPoints[0]);
                    SPDLOG_INFO("AirportSim/Staff: {} off duty at {:.0f}min", agent.name, dayMinutes);
                }
                break;
            }

            case EStaffState::Patrol:
            {
                if (!shiftActive)
                {
                    agent.sstate = EStaffState::OffDuty;
                    const FPointOfInterest* entrance = NearestOfCategory(map, "entrance", agent.position);
                    agents.MoveTo(agent, entrance != nullptr ? AirportMap::ServicePoint(*entrance, 0.0f)
                                                             : Config::kSpawnPoints[0]);
                    break;
                }
                if (agents.Arrived(agent))
                {
                    if (agent.stateUntil <= 0.0)
                    {
                        // 到点：保洁停留拖地 1~2 分钟，保安短暂驻足。
                        agent.stateUntil = gameMinutes + (agent.role == EAgentRole::Cleaner ? Rand(1.0, 2.0) : 0.4);
                        agent.anim = EAgentAnimHint::Work;
                    }
                    else if (gameMinutes >= agent.stateUntil)
                    {
                        agent.stateUntil = 0.0;
                        const char* const* patrol = agent.role == EAgentRole::Cleaner ? kCleanerPatrol : kGuardPatrol;
                        const int count = agent.role == EAgentRole::Cleaner
                                              ? static_cast<int>(std::size(kCleanerPatrol))
                                              : static_cast<int>(std::size(kGuardPatrol));
                        for (int tries = 0; tries < count; ++tries)
                        {
                            agent.patrolIdx = (agent.patrolIdx + 1) % count;
                            if (const FPointOfInterest* poi = map.FindByName(patrol[agent.patrolIdx]))
                            {
                                agents.MoveTo(agent, AirportMap::ServicePoint(*poi, Config::kGenericUseOffset));
                                break;
                            }
                        }
                    }
                }
                break;
            }

            case EStaffState::OffDuty:
                if (agents.Arrived(agent))
                {
                    if (glm::distance(glm::vec2(agent.position.x, agent.position.z),
                                      glm::vec2(Config::kSpawnPoints[agent.rosterIdx % 3].x,
                                                Config::kSpawnPoints[agent.rosterIdx % 3].z)) < 2.0f)
                    {
                        agents.Despawn(agent);
                    }
                    else
                    {
                        agents.MoveTo(agent, Config::kSpawnPoints[agent.rosterIdx % 3]);
                    }
                }
                break;

            default:
                break;
            }
        }
    }

    void JourneySystem::UpdateQueueStaffing(AgentSystem& agents, QueueSystem& queues)
    {
        for (const auto& q : queues.Queues())
        {
            bool staffed = false;
            for (const auto& agent : agents.Agents())
            {
                if (!agent.active || agent.sstate != EStaffState::OnDuty)
                {
                    continue;
                }
                const std::string& post =
                    agent.role == EAgentRole::GateAgent ? agent.assignedGate : agent.postPoi;
                if (post == q.id)
                {
                    staffed = true;
                    break;
                }
            }
            queues.SetStaffed(q.id, staffed);
        }
    }

    // ---------------- 旅客（§5.1） ----------------

    void JourneySystem::SpawnPassengers(double dayMinutes, AgentSystem& agents, FlightBoard& flights)
    {
        for (size_t fi = 0; fi < flights.FlightsMutable().size(); ++fi)
        {
            FFlight& flight = flights.FlightsMutable()[fi];
            const double spawnStart = flight.departMinutes - Config::kSpawnWindowStart;
            const double spawnEnd = flight.departMinutes - Config::kSpawnWindowEnd;
            // 已起飞/临近起飞的航班不再生成（跳时段/快进时直接放弃迟到旅客）。
            if (dayMinutes < spawnStart || dayMinutes > flight.departMinutes - Config::kBoardingLead ||
                flight.state >= EFlightState::Boarding || flight.paxSpawned >= flight.paxTotal)
            {
                continue;
            }
            const double progress = std::clamp((dayMinutes - spawnStart) / (spawnEnd - spawnStart), 0.0, 1.0);
            const int expected = static_cast<int>(std::ceil(progress * flight.paxTotal));
            if (flight.paxSpawned >= expected ||
                agents.ActivePassengerCount() >= Config::kMaxConcurrentPassengers)
            {
                continue;
            }

            const std::string name = Config::kPassengerNames[passengerNameCursor_ % Config::kPassengerNameCount];
            ++passengerNameCursor_;
            const std::string personality = Config::kPersonalities[rng_() % Config::kPersonalityCount];
            const float speedScale = 1.0f + Config::kWalkSpeedJitter * static_cast<float>(Rand(-1.0, 1.0));
            const glm::vec3 spawn = Config::kSpawnPoints[rng_() % 3];

            if (FAgent* pax = agents.SpawnPassenger(name, personality, spawn, speedScale))
            {
                pax->flightIdx = static_cast<int>(fi);
                ++flight.paxSpawned;
            }
        }
    }

    void JourneySystem::StartAirsideActivity(FAgent& agent, AgentSystem& agents, AirportMap& map, double gameMinutes)
    {
        // 规则 fallback 活动选择（§5.1 AirsideFree）：LLM 结果会通过 ApplyAirsideChoice 覆盖下一轮。
        // 权重：坐下 37 / 餐饮 24 / 零售街 18 / 售货机·ATM 13 / 如厕 8——空侧消费区要有人气。
        const int roll = static_cast<int>(rng_() % 100);
        std::string category;
        if (roll < 30)      category = "wait";
        else if (roll < 42) category = "cafe";
        else if (roll < 54) category = "food";
        else if (roll < 72) category = (rng_() % 3 == 0) ? "book" : (rng_() % 2 == 0 ? "shop" : "gift");
        else if (roll < 80) category = "vending";
        else if (roll < 85) category = "atm";
        else if (roll < 93) category = "toilet";
        else                category = "wait";

        if (category == "wait")
        {
            // 找有空位的候机椅。
            auto benches = map.PointsOfCategory("wait");
            std::shuffle(benches.begin(), benches.end(), rng_);
            for (const auto* bench : benches)
            {
                glm::vec3 seatPos;
                const int slot = map.ClaimSeat(bench->name, agent.id, seatPos);
                if (slot >= 0)
                {
                    agent.seatPoi = bench->name;
                    agent.seatSlot = slot;
                    agent.targetPoi = bench->name;
                    agent.pstate = EPassengerState::AirsideWalk;
                    agents.MoveTo(agent, seatPos);
                    return;
                }
            }
            category = "vending"; // 全坐满 → 改买饮料
        }

        const auto pois = map.PointsOfCategory(category);
        if (pois.empty())
        {
            agent.pstate = EPassengerState::AirsideIdle;
            agent.stateUntil = gameMinutes + 2.0;
            return;
        }
        const FPointOfInterest* poi = pois[rng_() % pois.size()];
        agent.targetPoi = poi->name;
        agent.pstate = EPassengerState::AirsideWalk;
        agents.MoveTo(agent, AirportMap::ServicePoint(*poi, Config::kGenericUseOffset));
    }

    void JourneySystem::ApplyAirsideChoice(FAgent& agent, const std::string& poiName, AgentSystem& agents,
                                           AirportMap& map)
    {
        const FPointOfInterest* poi = map.FindByName(poiName);
        if (poi == nullptr || !IsAirsideLeisureCategory(poi->category))
        {
            return;
        }
        if (agent.pstate != EPassengerState::AirsideIdle && agent.pstate != EPassengerState::AirsideWalk &&
            agent.pstate != EPassengerState::AirsideUse)
        {
            return;
        }

        // 打断当前活动，去 LLM 选的点。
        if (!agent.seatPoi.empty())
        {
            map.ReleaseSeat(agent.seatPoi, agent.seatSlot, agent.id);
            agent.seatPoi.clear();
            agent.seatSlot = -1;
        }
        if (!agent.targetPoi.empty())
        {
            map.Release(agent.targetPoi, agent.id);
        }
        agent.anim = EAgentAnimHint::Idle;

        if (poi->category == "wait")
        {
            glm::vec3 seatPos;
            const int slot = map.ClaimSeat(poi->name, agent.id, seatPos);
            if (slot < 0)
            {
                return;
            }
            agent.seatPoi = poi->name;
            agent.seatSlot = slot;
            agent.targetPoi = poi->name;
            agent.pstate = EPassengerState::AirsideWalk;
            agents.MoveTo(agent, seatPos);
            return;
        }

        agent.targetPoi = poi->name;
        agent.pstate = EPassengerState::AirsideWalk;
        agents.MoveTo(agent, AirportMap::ServicePoint(*poi, Config::kGenericUseOffset));
    }

    void JourneySystem::TickPassengers(double gameMinutes, double dayMinutes, AgentSystem& agents, AirportMap& map,
                                       QueueSystem& queues, FlightBoard& flights)
    {
        // 队列服务完成事件。
        const std::vector<int> completed = queues.ConsumeCompleted();

        for (auto& agent : agents.Agents())
        {
            if (!agent.active || agent.role != EAgentRole::Passenger)
            {
                continue;
            }
            if (agent.flightIdx < 0 || agent.flightIdx >= static_cast<int>(flights.Flights().size()))
            {
                DespawnPassenger(agent, agents, map, queues);
                continue;
            }
            const FFlight& flight = flights.Flights()[static_cast<size_t>(agent.flightIdx)];
            const double toDepart = flight.departMinutes - dayMinutes;
            const bool serviceDone = std::find(completed.begin(), completed.end(), agent.id) != completed.end();

            // 起飞：未登机旅客直接"传送登机"（§4.3，MVP 不做误机）。
            if (flight.state == EFlightState::Departed)
            {
                SPDLOG_INFO("AirportSim/Journey: {} teleport-boarded departed {}", agent.name, flight.number);
                DespawnPassenger(agent, agents, map, queues);
                continue;
            }

            switch (agent.pstate)
            {
            case EPassengerState::ToEntrance:
                if (agent.targetPoi.empty())
                {
                    if (const FPointOfInterest* entrance = NearestOfCategory(map, "entrance", agent.position))
                    {
                        agent.targetPoi = entrance->name;
                        agents.MoveTo(agent, AirportMap::ServicePoint(*entrance, 0.0f));
                    }
                }
                else if (agents.Arrived(agent))
                {
                    agent.targetPoi.clear();
                    // 30% 自助 kiosk，70% 人工值机（§5.1）。
                    FPointOfInterest* kiosk = nullptr;
                    if (Rand(0.0, 1.0) < Config::kKioskFraction)
                    {
                        kiosk = map.ClaimFree("kiosk", agent.id);
                    }
                    if (kiosk != nullptr)
                    {
                        agent.targetPoi = kiosk->name;
                        agent.pstate = EPassengerState::ToKiosk;
                        agents.MoveTo(agent, AirportMap::ServicePoint(*kiosk, Config::kGenericUseOffset));
                    }
                    else
                    {
                        const std::string queueId = queues.ShortestQueue("checkin");
                        if (!queueId.empty())
                        {
                            agent.queueId = queueId;
                            agent.queuedSlot = -1;
                            agent.pstate = EPassengerState::QueueCheckin;
                            queues.Join(queueId, agent.id);
                        }
                    }
                }
                break;

            case EPassengerState::ToKiosk:
                if (agents.Arrived(agent))
                {
                    agent.pstate = EPassengerState::UseKiosk;
                    agent.stateUntil = gameMinutes + Rand(Config::kKioskServiceMin, Config::kKioskServiceMax);
                    agent.anim = EAgentAnimHint::Work;
                }
                break;

            case EPassengerState::UseKiosk:
                if (gameMinutes >= agent.stateUntil)
                {
                    map.Release(agent.targetPoi, agent.id);
                    agent.targetPoi.clear();
                    agent.anim = EAgentAnimHint::Idle;
                    const std::string queueId = queues.ShortestQueue("security");
                    if (!queueId.empty())
                    {
                        agent.queueId = queueId;
                        agent.queuedSlot = -1;
                        agent.pstate = EPassengerState::QueueSecurity;
                        queues.Join(queueId, agent.id);
                    }
                }
                break;

            case EPassengerState::QueueCheckin:
            case EPassengerState::QueueSecurity:
            case EPassengerState::QueueGate:
            {
                if (serviceDone)
                {
                    const std::string doneQueue = agent.queueId;
                    agent.queueId.clear();
                    agent.queuedSlot = -1;
                    if (agent.pstate == EPassengerState::QueueCheckin)
                    {
                        const std::string next = queues.ShortestQueue("security");
                        if (!next.empty())
                        {
                            agent.queueId = next;
                            agent.pstate = EPassengerState::QueueSecurity;
                            queues.Join(next, agent.id);
                        }
                    }
                    else if (agent.pstate == EPassengerState::QueueSecurity)
                    {
                        // 安检通过：脚本走点南进北出（§7.2 单向流）。
                        if (const FPointOfInterest* lane = map.FindByName(doneQueue))
                        {
                            std::vector<glm::vec3> wps;
                            wps.push_back(AirportMap::ServicePoint(*lane, Config::kSecurityEntryOffset * 0.4f));
                            wps.push_back(lane->worldPos);
                            wps.push_back(AirportMap::ServicePoint(*lane, -Config::kSecurityPassDepth));
                            agents.MoveAlong(agent, std::move(wps));
                        }
                        agent.pstate = EPassengerState::PassSecurity;
                    }
                    else // QueueGate：检票完成 → 进 gate 门消失
                    {
                        FFlight& f = flights.FlightsMutable()[static_cast<size_t>(agent.flightIdx)];
                        ++f.paxBoarded;
                        SPDLOG_INFO("AirportSim/Journey: {} boarded {} ({}/{})", agent.name, f.number, f.paxBoarded,
                                    f.paxTotal);
                        DespawnPassenger(agent, agents, map, queues);
                    }
                    break;
                }

                // 整队前移：slot 变化就走向新站位。
                const int slot = queues.SlotOf(agent.queueId, agent.id);
                if (slot >= 0 && slot != agent.queuedSlot)
                {
                    agent.queuedSlot = slot;
                    agents.MoveTo(agent, queues.SlotPosition(agent.queueId, slot));
                }
                break;
            }

            case EPassengerState::PassSecurity:
                if (agents.Arrived(agent))
                {
                    agent.pstate = EPassengerState::AirsideIdle;
                    agent.stateUntil = 0.0;
                    agent.nextDecisionAt = gameMinutes; // 进入 Layer 1 决策循环
                }
                break;

            case EPassengerState::AirsideIdle:
            case EPassengerState::AirsideWalk:
            case EPassengerState::AirsideUse:
            {
                // 富余时间不足或航班开始登机 → 强制收敛去 gate（Layer 0 刚性规则）。
                // 富余 = 距登机(起飞-30)的剩余时间；<15 分钟即收敛。
                if (toDepart < Config::kForceToGateMinutes + Config::kBoardingLead ||
                    flight.state == EFlightState::Boarding || flight.state == EFlightState::Final)
                {
                    ReleaseClaims(agent, map, queues);
                    agent.anim = EAgentAnimHint::Idle;
                    agent.queueId = flight.gatePoi;
                    agent.queuedSlot = -1;
                    agent.pstate = EPassengerState::QueueGate;
                    queues.Join(flight.gatePoi, agent.id);
                    break;
                }

                if (agent.pstate == EPassengerState::AirsideIdle)
                {
                    if (gameMinutes >= agent.stateUntil)
                    {
                        StartAirsideActivity(agent, agents, map, gameMinutes);
                    }
                }
                else if (agent.pstate == EPassengerState::AirsideWalk)
                {
                    if (agents.Arrived(agent))
                    {
                        agent.pstate = EPassengerState::AirsideUse;
                        const bool sitting = !agent.seatPoi.empty();
                        if (sitting)
                        {
                            agent.stateUntil = gameMinutes + Rand(Config::kSitMin, Config::kSitMax);
                            agent.anim = EAgentAnimHint::Sit;
                            // 坐下时面向椅子 front。
                            if (const FPointOfInterest* bench = map.FindByName(agent.seatPoi))
                            {
                                agent.yaw = std::atan2(bench->frontDir.x, bench->frontDir.z);
                            }
                        }
                        else if (const FPointOfInterest* poi = map.FindByName(agent.targetPoi))
                        {
                            double useMin = Config::kShopUseMin, useMax = Config::kShopUseMax;
                            if (poi->category == "toilet")
                            {
                                useMin = Config::kToiletUseMin;
                                useMax = Config::kToiletUseMax;
                            }
                            else if (poi->category == "vending" || poi->category == "atm")
                            {
                                useMin = Config::kVendingUseMin;
                                useMax = Config::kVendingUseMax;
                            }
                            agent.stateUntil = gameMinutes + Rand(useMin, useMax);
                            agent.anim = EAgentAnimHint::Work;
                            agent.yaw = std::atan2(-poi->frontDir.x, -poi->frontDir.z); // 面向服务台
                        }
                    }
                }
                else // AirsideUse
                {
                    if (gameMinutes >= agent.stateUntil)
                    {
                        if (!agent.seatPoi.empty())
                        {
                            map.ReleaseSeat(agent.seatPoi, agent.seatSlot, agent.id);
                            agent.seatPoi.clear();
                            agent.seatSlot = -1;
                        }
                        map.Release(agent.targetPoi, agent.id);
                        agent.targetPoi.clear();
                        agent.anim = EAgentAnimHint::Idle;
                        agent.pstate = EPassengerState::AirsideIdle;
                        agent.stateUntil = gameMinutes + Rand(0.5, 2.0);
                    }
                }
                break;
            }

            default:
                break;
            }
        }
    }
}
