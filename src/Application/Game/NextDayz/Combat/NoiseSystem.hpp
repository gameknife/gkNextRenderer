#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

namespace NextDayz
{
    enum class ENoiseType : uint8_t
    {
        Footstep,
        Melee,
        Pistol,
        Rifle,
        Shotgun,
        Impact,
    };

    struct FNoiseEvent
    {
        uint64_t sequence = 0;
        glm::vec3 position{};
        float radius = 0.0f;
        float strength = 0.0f;
        ENoiseType type = ENoiseType::Footstep;
        float remainingSeconds = 0.0f;
    };

    class NoiseSystem
    {
    public:
        uint64_t Emit(const glm::vec3& position, float radius, float strength, ENoiseType type,
                      float lifetimeSeconds = 1.25f);
        void Update(float deltaSeconds);
        void Reset();
        const std::vector<FNoiseEvent>& Events() const { return events_; }
        uint64_t LastSequence() const { return sequence_; }

    private:
        uint64_t sequence_ = 0;
        std::vector<FNoiseEvent> events_;
    };
}
