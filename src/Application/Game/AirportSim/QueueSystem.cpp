#include "QueueSystem.h"

#include "AirportSimConfig.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace AirportSim
{
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

    void QueueSystem::Tick(double gameMinutes)
    {
        for (auto& q : queues_)
        {
            if (!q.staffed || q.agents.empty())
            {
                q.serving = false;
                continue;
            }
            if (!q.serving)
            {
                q.serving = true;
                std::uniform_real_distribution<double> dist(q.serviceMin, q.serviceMax);
                q.serviceEndAt = gameMinutes + dist(rng_);
            }
            else if (gameMinutes >= q.serviceEndAt)
            {
                completed_.push_back(q.agents.front());
                q.agents.erase(q.agents.begin());
                q.serving = false;
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
