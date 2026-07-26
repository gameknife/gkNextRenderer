#pragma once

#include <array>
#include <string>

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

    enum class EPlayerJumpPhase
    {
        None,
        Up,
        AirLoop,
        Down,
    };

    enum class EPlayerTraversalAction
    {
        None,
        Vault,
        ClimbUp,
    };

    enum class EPlayerAction
    {
        None,
        LootGround,
    };

    enum class EWeaponPresentationAction
    {
        None,
        Reload,
        Switch,
    };

    struct FPlayerLocomotionState
    {
        EPlayerStance desiredStance = EPlayerStance::Standing;
        EPlayerStance actualStance = EPlayerStance::Standing;
        EPlayerGait gait = EPlayerGait::Idle;
        glm::vec2 localMove{0.0f}; // x=right, y=forward
        glm::vec3 worldVelocity{0.0f};
        float horizontalSpeed = 0.0f;
        EPlayerJumpPhase jumpPhase = EPlayerJumpPhase::None;
        float jumpPhaseTime01 = 0.0f;
        EPlayerTraversalAction traversalAction = EPlayerTraversalAction::None;
        float traversalTime01 = 0.0f;
        float traversalHeight = 0.0f;
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
        std::string weaponId;
        EWeaponPresentationAction weaponAction = EWeaponPresentationAction::None;
        float weaponActionTime01 = 0.0f;
        int activeWeaponSlot = 0;
        std::array<std::string, 2> slotWeaponIds;
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

    inline const char* JumpPhaseName(EPlayerJumpPhase phase)
    {
        switch (phase)
        {
        case EPlayerJumpPhase::Up: return "up";
        case EPlayerJumpPhase::AirLoop: return "air_loop";
        case EPlayerJumpPhase::Down: return "down";
        case EPlayerJumpPhase::None:
        default: return "none";
        }
    }

    inline const char* TraversalActionName(EPlayerTraversalAction action)
    {
        switch (action)
        {
        case EPlayerTraversalAction::Vault: return "vault";
        case EPlayerTraversalAction::ClimbUp: return "climb_up";
        case EPlayerTraversalAction::None:
        default: return "none";
        }
    }

    inline const char* ActionName(EPlayerAction action)
    {
        return action == EPlayerAction::LootGround ? "loot_ground" : "none";
    }

    inline const char* WeaponActionName(EWeaponPresentationAction action)
    {
        switch (action)
        {
        case EWeaponPresentationAction::Reload: return "reload";
        case EWeaponPresentationAction::Switch: return "switch";
        case EWeaponPresentationAction::None:
        default: return "none";
        }
    }
}
