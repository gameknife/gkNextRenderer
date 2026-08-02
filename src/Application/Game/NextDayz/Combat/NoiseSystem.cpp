#include "Engine/Common/CoreMinimal.hpp"

#include "NoiseSystem.hpp"

namespace NextDayz
{
    uint64_t NoiseSystem::Emit(const glm::vec3& position, float radius, float strength, ENoiseType type,
                               float lifetimeSeconds)
    {
        const uint64_t sequence = ++sequence_;
        events_.push_back({sequence, position, std::max(radius, 0.0f), std::max(strength, 0.0f), type,
                           std::max(lifetimeSeconds, 0.0f)});
        return sequence;
    }

    void NoiseSystem::Update(float deltaSeconds)
    {
        const float delta = std::max(deltaSeconds, 0.0f);
        for (FNoiseEvent& event : events_)
        {
            event.remainingSeconds -= delta;
        }
        events_.erase(std::remove_if(events_.begin(), events_.end(),
            [](const FNoiseEvent& event) { return event.remainingSeconds <= 0.0f; }), events_.end());
    }

    void NoiseSystem::Reset()
    {
        events_.clear();
        sequence_ = 0;
    }
}
