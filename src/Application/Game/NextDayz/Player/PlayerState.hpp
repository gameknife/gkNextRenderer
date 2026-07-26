#pragma once

#include <glm/glm.hpp>

namespace NextDayz
{
    enum class EPlayerStance
    {
        Standing,
        Crouched,
    };

    enum class EPlayerGait
    {
        Idle,
        Walk,
        Run,
        Sprint,
    };

    enum class EPlayerAction
    {
        None,
        LootGround,
    };

    struct FPlayerLocomotionState
    {
        EPlayerStance desiredStance = EPlayerStance::Standing;
        EPlayerStance actualStance = EPlayerStance::Standing;
        EPlayerGait gait = EPlayerGait::Idle;
        glm::vec2 localMove{0.0f}; // x=right, y=forward
        glm::vec3 worldVelocity{0.0f};
        float horizontalSpeed = 0.0f;
        bool onGround = false;
        bool standBlocked = false;
    };

    struct FPlayerPresentationState
    {
        FPlayerLocomotionState locomotion;
        EPlayerAction action = EPlayerAction::None;
        float actionTime01 = 0.0f;
        float aimWeight = 0.0f;
        float aimPitchRadians = 0.0f;
        bool hasWeapon = false;
    };

    inline const char* StanceName(EPlayerStance stance)
    {
        return stance == EPlayerStance::Crouched ? "crouched" : "standing";
    }

    inline const char* GaitName(EPlayerGait gait)
    {
        switch (gait)
        {
        case EPlayerGait::Walk: return "walk";
        case EPlayerGait::Run: return "run";
        case EPlayerGait::Sprint: return "sprint";
        case EPlayerGait::Idle:
        default: return "idle";
        }
    }

    inline const char* ActionName(EPlayerAction action)
    {
        return action == EPlayerAction::LootGround ? "loot_ground" : "none";
    }
}
