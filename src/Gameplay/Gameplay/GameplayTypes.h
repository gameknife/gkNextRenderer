#pragma once

#include "Engine/Common/CoreMinimal.hpp"

namespace NextGameplay
{
    enum class EBehaviorTreeStatus
    {
        Failure,
        Success,
        Running,
    };

    enum class EBehaviorDebugState
    {
        Inactive,
        Failure,
        Success,
        Running,
    };

    enum class ECharacterAnimState
    {
        Idle,
        WalkForward,
        WalkBackward,
        WalkStrafeLeft,
        WalkStrafeRight,
        RunForward,
        RunBackward,
        RunStrafeLeft,
        RunStrafeRight,
        JumpStart,
        JumpLoop,
        JumpLand,
    };

    enum class ECharacterMovementMode
    {
        CameraAligned,
        MoveAligned,
    };

    enum class ECharacterControlSource
    {
        Player,
        AI,
    };

    enum class EAIAgentState
    {
        Disabled,
        Patrol,
        Chase,
        Evade,
        Attack,
    };

    inline const char* GetBehaviorDebugStateName(EBehaviorDebugState state)
    {
        switch (state)
        {
        case EBehaviorDebugState::Inactive:
            return "Inactive";
        case EBehaviorDebugState::Failure:
            return "Failure";
        case EBehaviorDebugState::Success:
            return "Success";
        case EBehaviorDebugState::Running:
            return "Running";
        default:
            return "Unknown";
        }
    }

    inline EBehaviorDebugState ToBehaviorDebugState(EBehaviorTreeStatus status)
    {
        switch (status)
        {
        case EBehaviorTreeStatus::Failure:
            return EBehaviorDebugState::Failure;
        case EBehaviorTreeStatus::Success:
            return EBehaviorDebugState::Success;
        case EBehaviorTreeStatus::Running:
            return EBehaviorDebugState::Running;
        default:
            return EBehaviorDebugState::Inactive;
        }
    }

    inline const char* GetCharacterAnimStateName(ECharacterAnimState state)
    {
        switch (state)
        {
        case ECharacterAnimState::Idle:
            return "Idle";
        case ECharacterAnimState::WalkForward:
            return "WalkForward";
        case ECharacterAnimState::WalkBackward:
            return "WalkBackward";
        case ECharacterAnimState::WalkStrafeLeft:
            return "WalkStrafeLeft";
        case ECharacterAnimState::WalkStrafeRight:
            return "WalkStrafeRight";
        case ECharacterAnimState::RunForward:
            return "RunForward";
        case ECharacterAnimState::RunBackward:
            return "RunBackward";
        case ECharacterAnimState::RunStrafeLeft:
            return "RunStrafeLeft";
        case ECharacterAnimState::RunStrafeRight:
            return "RunStrafeRight";
        case ECharacterAnimState::JumpStart:
            return "JumpStart";
        case ECharacterAnimState::JumpLoop:
            return "JumpLoop";
        case ECharacterAnimState::JumpLand:
            return "JumpLand";
        default:
            return "Unknown";
        }
    }

    inline const char* GetCharacterMovementModeName(ECharacterMovementMode mode)
    {
        switch (mode)
        {
        case ECharacterMovementMode::CameraAligned:
            return "CameraAligned";
        case ECharacterMovementMode::MoveAligned:
            return "MoveAligned";
        default:
            return "Unknown";
        }
    }

    inline const char* GetCharacterControlSourceName(ECharacterControlSource source)
    {
        switch (source)
        {
        case ECharacterControlSource::Player:
            return "Player";
        case ECharacterControlSource::AI:
            return "AI";
        default:
            return "Unknown";
        }
    }

    inline const char* GetAIAgentStateName(EAIAgentState state)
    {
        switch (state)
        {
        case EAIAgentState::Disabled:
            return "Disabled";
        case EAIAgentState::Patrol:
            return "Patrol";
        case EAIAgentState::Chase:
            return "Chase";
        case EAIAgentState::Evade:
            return "Evade";
        case EAIAgentState::Attack:
            return "Attack";
        default:
            return "Unknown";
        }
    }
}
