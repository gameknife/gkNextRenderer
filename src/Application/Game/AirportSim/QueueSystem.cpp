#include "QueueSystem.h"

#include "AgentSystem.h"
#include "AirportSimConfig.hpp"

#include <algorithm>
#include <cmath>

#include <spdlog/spdlog.h>

namespace AirportSim
{
    namespace
    {
        bool StartsWith(const std::string& value, const char* prefix)
        {
            return value.rfind(prefix, 0) == 0;
        }
    }

    void QueueSystem::Reset(unsigned seed)
    {
        queues_.clear();
        completed_.clear();
        rng_.seed(seed);
    }

    QueueSystem::FQueue* QueueSystem::Find(const std::string& id)
    {
        for (auto& q : queues_)
        {
            if (q.id == id)
            {
                return &q;
            }
        }
        return nullptr;
    }

    const QueueSystem::FQueue* QueueSystem::Find(const std::string& id) const
    {
        for (const auto& q : queues_)
        {
            if (q.id == id)
            {
                return &q;
            }
        }
        return nullptr;
    }

    void QueueSystem::EnsureQueue(const std::string& id, const glm::vec3& servicePoint, const glm::vec3& queueDir,
                                  double serviceMin, double serviceMax)
    {
        if (Find(id) != nullptr)
        {
            return;
        }
        FQueue q;
        q.id = id;
        q.servicePoint = servicePoint;
        q.queueDir = queueDir;
        q.serviceMin = serviceMin;
        q.serviceMax = serviceMax;
        queues_.push_back(std::move(q));
    }

    void QueueSystem::SetStaffed(const std::string& id, bool staffed)
    {
        if (FQueue* q = Find(id))
        {
            q->staffed = staffed;
        }
    }

    int QueueSystem::Join(const std::string& id, int agentId)
    {
        FQueue* q = Find(id);
        if (q == nullptr)
        {
            return -1;
        }
        for (size_t i = 0; i < q->agents.size(); ++i)
        {
            if (q->agents[i] == agentId)
            {
                return static_cast<int>(i);
            }
        }
        q->agents.push_back(agentId);
        return static_cast<int>(q->agents.size()) - 1;
    }

    void QueueSystem::Leave(const std::string& id, int agentId)
    {
        FQueue* q = Find(id);
        if (q == nullptr)
        {
            return;
        }
        const bool wasFront = !q->agents.empty() && q->agents.front() == agentId;
        q->agents.erase(std::remove(q->agents.begin(), q->agents.end(), agentId), q->agents.end());
        if (wasFront)
        {
            q->serving = false;
            q->serviceStartedAt = 0.0;
            q->serviceEndAt = 0.0;
            q->currentServiceMinutes = 0.0;
        }
    }

    int QueueSystem::SlotOf(const std::string& id, int agentId) const
    {
        const FQueue* q = Find(id);
        if (q == nullptr)
        {
            return -1;
        }
        for (size_t i = 0; i < q->agents.size(); ++i)
        {
            if (q->agents[i] == agentId)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    glm::vec3 QueueSystem::SlotPosition(const std::string& id, int slot) const
    {
        const FQueue* q = Find(id);
        if (q == nullptr || slot < 0)
        {
            return glm::vec3(0.0f, Config::kGroundY, 0.0f);
        }
        const float distance = Config::kQueueFirstSlotOffset +
                               Config::kQueueSlotSpacing * static_cast<float>(std::min(slot, Config::kQueueMaxSlots));
        return q->servicePoint + q->queueDir * distance;
    }

    int QueueSystem::Length(const std::string& id) const
    {
        const FQueue* q = Find(id);
        return q != nullptr ? static_cast<int>(q->agents.size()) : 0;
    }

    bool QueueSystem::IsStaffed(const std::string& id) const
    {
        const FQueue* q = Find(id);
        return q != nullptr && q->staffed;
    }

    std::string QueueSystem::ShortestQueue(const std::string& prefix) const
    {
        const FQueue* bestStaffed = nullptr;
        const FQueue* bestAny = nullptr;
        for (const auto& q : queues_)
        {
            if (q.id.rfind(prefix, 0) != 0)
            {
                continue;
            }
            if (bestAny == nullptr || q.agents.size() < bestAny->agents.size())
            {
                bestAny = &q;
            }
            if (q.staffed && (bestStaffed == nullptr || q.agents.size() < bestStaffed->agents.size()))
            {
                bestStaffed = &q;
            }
        }
        const FQueue* best = bestStaffed != nullptr ? bestStaffed : bestAny;
        return best != nullptr ? best->id : std::string();
    }

    void QueueSystem::Tick(double gameMinutes, const AgentSystem& agents)
    {
        for (auto& q : queues_)
        {
            if (!q.staffed || q.agents.empty())
            {
                q.serving = false;
                q.serviceStartedAt = 0.0;
                q.serviceEndAt = 0.0;
                q.currentServiceMinutes = 0.0;
                continue;
            }
            if (!q.serving)
            {
                const FAgent* passenger = agents.FindById(q.agents.front());
                if (passenger == nullptr || !agents.Arrived(*passenger))
                {
                    continue;
                }

                std::uniform_real_distribution<double> baseDist(q.serviceMin, q.serviceMax);
                std::uniform_real_distribution<double> facilityDist(Config::kFacilityDelayMultiplierMin,
                                                                    Config::kFacilityDelayMultiplierMax);
                std::uniform_real_distribution<double> passengerDist(Config::kPassengerDelayMultiplierMin,
                                                                     Config::kPassengerDelayMultiplierMax);
                std::uniform_real_distribution<double> unitDist(0.0, 1.0);

                double duration = baseDist(rng_);
                if (StartsWith(q.id, "checkin") || StartsWith(q.id, "security"))
                {
                    const double waitingPassengers = static_cast<double>(q.agents.size() - 1);
                    const double congestionMultiplier =
                        std::min(Config::kCongestionDelayMultiplierMax,
                                 1.0 + waitingPassengers * Config::kCongestionDelayPerWaitingPassenger);
                    double passengerMultiplier = passengerDist(rng_);
                    if (passenger->IsGroupedPassenger())
                    {
                        passengerMultiplier += 0.05 * static_cast<double>(passenger->groupSize - 1);
                    }
                    if (passenger->personality == "急躁")
                    {
                        passengerMultiplier += 0.08;
                    }
                    duration *= facilityDist(rng_) * congestionMultiplier * passengerMultiplier;
                    if (unitDist(rng_) < Config::kServiceIncidentChance)
                    {
                        std::uniform_real_distribution<double> incidentDist(
                            Config::kServiceIncidentMultiplierMin, Config::kServiceIncidentMultiplierMax);
                        duration *= incidentDist(rng_);
                    }
                }

                q.serving = true;
                q.serviceStartedAt = gameMinutes;
                q.currentServiceMinutes = duration;
                q.serviceEndAt = gameMinutes + duration;
                SPDLOG_DEBUG("AirportSim/Queue: {} serving {} for {:.1f} min (waiting {})", q.id,
                             passenger->name, duration, q.agents.size() - 1);
            }
            else if (gameMinutes >= q.serviceEndAt)
            {
                completed_.push_back(q.agents.front());
                q.agents.erase(q.agents.begin());
                q.serving = false;
                q.serviceStartedAt = 0.0;
                q.serviceEndAt = 0.0;
                q.currentServiceMinutes = 0.0;
            }
        }
    }

    std::vector<int> QueueSystem::ConsumeCompleted()
    {
        std::vector<int> out;
        out.swap(completed_);
        return out;
    }
}
