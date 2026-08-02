#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

#include <compare>

namespace NextDayz
{
    enum class EHitZone : uint8_t
    {
        Head,
        Torso,
        Limb,
    };

    struct FZombieHandle
    {
        uint32_t index = 0;
        uint32_t generation = 0;

        bool IsValid() const { return generation != 0; }
        auto operator<=>(const FZombieHandle&) const = default;
    };

    struct FWeaponHitEvent
    {
        uint64_t sequence = 0;
        std::string weaponId;
        glm::vec3 origin{};
        glm::vec3 direction{};
        uint32_t hitInstanceId = 0;
        glm::vec3 hitPoint{};
        float baseDamage = 0.0f;
    };

    struct FDamageEvent
    {
        uint64_t sequence = 0;
        FZombieHandle target{};
        EHitZone hitZone = EHitZone::Torso;
        float amount = 0.0f;
        glm::vec3 worldPoint{};
    };
}
