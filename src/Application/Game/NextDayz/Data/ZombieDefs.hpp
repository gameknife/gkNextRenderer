#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <glm/vec3.hpp>

namespace NextDayz
{
    enum class EZombieProfile : uint8_t
    {
        Civilian,
        Industrial,
        Military,
        Wilderness,
    };

    struct FZombieDef
    {
        EZombieProfile profile = EZombieProfile::Civilian;
        std::string_view id = "civilian";
        float maxHealth = 100.0f;
        float wanderSpeed = 1.0f;
        float chaseSpeed = 3.2f;
        float sightDistance = 35.0f;
        float fieldOfViewDegrees = 110.0f;
        float attackDamage = 12.0f;
        float attackRange = 1.25f;
        glm::vec3 tint{0.46f, 0.54f, 0.42f};
    };

    inline constexpr std::array<FZombieDef, 4> zombieDefs = {{
        {EZombieProfile::Civilian, "civilian", 90.0f, 0.9f, 3.0f, 34.0f, 110.0f, 11.0f, 1.25f,
         {0.48f, 0.56f, 0.44f}},
        {EZombieProfile::Industrial, "industrial", 110.0f, 0.85f, 2.9f, 32.0f, 105.0f, 14.0f, 1.3f,
         {0.58f, 0.48f, 0.28f}},
        {EZombieProfile::Military, "military", 135.0f, 1.0f, 3.35f, 40.0f, 115.0f, 16.0f, 1.3f,
         {0.30f, 0.42f, 0.24f}},
        {EZombieProfile::Wilderness, "wilderness", 100.0f, 1.05f, 3.15f, 38.0f, 120.0f, 12.0f, 1.25f,
         {0.42f, 0.36f, 0.30f}},
    }};

    inline const FZombieDef& ZombieDef(EZombieProfile profile)
    {
        return zombieDefs[static_cast<size_t>(profile)];
    }
}
