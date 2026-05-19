#pragma once

#include "Engine/Assets/Core/Component.h"
#include "Engine/NextGameplay/AI/PathFollower.h"
#include "Engine/NextGameplay/Gameplay/GameplayTypes.h"
#include "Engine/Runtime/Reflection/ReflectionMacros.h"

namespace NextGameplay
{
    class AIAgentComponent : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(AIAgentComponent)

        AIAgentComponent() = default;

        void SetEnabled(bool enabled) { enabled_ = enabled; }
        bool GetEnabled() const { return enabled_; }

        void SetState(EAIAgentState state) { state_ = state; }
        EAIAgentState GetState() const { return state_; }

        void SetDesiredState(EAIAgentState state) { desiredState_ = state; }
        EAIAgentState GetDesiredState() const { return desiredState_; }

        void SetPreviousState(EAIAgentState state) { previousState_ = state; }
        EAIAgentState GetPreviousState() const { return previousState_; }

        void SetTargetVisible(bool visible) { targetVisible_ = visible; }
        bool GetTargetVisible() const { return targetVisible_; }

        void SetBehaviorRootStatus(EBehaviorDebugState state) { behaviorRootStatus_ = state; }
        EBehaviorDebugState GetBehaviorRootStatus() const { return behaviorRootStatus_; }

        void SetBehaviorEvadeStatus(EBehaviorDebugState state) { behaviorEvadeStatus_ = state; }
        EBehaviorDebugState GetBehaviorEvadeStatus() const { return behaviorEvadeStatus_; }

        void SetBehaviorAttackStatus(EBehaviorDebugState state) { behaviorAttackStatus_ = state; }
        EBehaviorDebugState GetBehaviorAttackStatus() const { return behaviorAttackStatus_; }

        void SetBehaviorChaseStatus(EBehaviorDebugState state) { behaviorChaseStatus_ = state; }
        EBehaviorDebugState GetBehaviorChaseStatus() const { return behaviorChaseStatus_; }

        void SetBehaviorPatrolStatus(EBehaviorDebugState state) { behaviorPatrolStatus_ = state; }
        EBehaviorDebugState GetBehaviorPatrolStatus() const { return behaviorPatrolStatus_; }

        void ResetRuntimeState(bool enabled, const glm::vec3& initialTargetPosition);
        void ResetDebugState();

        // Runtime-only state owned by AI systems.
        std::vector<glm::vec3> patrolPoints;
        glm::vec3 moveDir{0.0f};
        glm::vec3 lookDir{0.0f, 0.0f, 1.0f};
        glm::vec3 lastKnownTargetPosition{0.0f};
        float yaw = 0.0f;
        float fireCooldownRemaining = 0.0f;
        float targetMemoryRemaining = 0.0f;
        float patrolPauseRemaining = 0.0f;
        float targetVisibleGraceRemaining = 0.0f;
        float stateHoldRemaining = 0.0f;
        float strafeSign = 1.0f;
        size_t patrolIndex = 0;
        bool triggerJump = false;
        bool patrolReachableFound = false;
        bool patrolUsedNearFallback = false;
        size_t patrolRequestedIndex = 0;
        size_t patrolSelectedIndex = 0;
        int patrolCandidatesTested = 0;
        int patrolWaypointCount = 0;
        float patrolSelectionMs = 0.0f;
        float patrolSelectedDistance = 0.0f;
        float patrolStuckTime = 0.0f;
        bool patrolAbandonedTarget = false;
        size_t patrolLastCommittedIndex = std::numeric_limits<size_t>::max();
        glm::vec3 patrolSelectedTarget{0.0f};
        glm::vec3 patrolProgressAnchor{0.0f};
        FPathFollower pathFollower;

    private:
        bool enabled_ = true;
        EAIAgentState state_ = EAIAgentState::Disabled;
        EAIAgentState desiredState_ = EAIAgentState::Disabled;
        EAIAgentState previousState_ = EAIAgentState::Disabled;
        bool targetVisible_ = false;
        EBehaviorDebugState behaviorRootStatus_ = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorEvadeStatus_ = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorAttackStatus_ = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorChaseStatus_ = EBehaviorDebugState::Inactive;
        EBehaviorDebugState behaviorPatrolStatus_ = EBehaviorDebugState::Inactive;
    };
}
