#include "NextGameplay/Components/AIAgentComponent.h"

#include "Runtime/Reflection/PropertyMeta.h"

#include <entt/meta/factory.hpp>

namespace NextGameplay
{
    void AIAgentComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;

        entt::meta_factory<EAIAgentState>()
            .type("EAIAgentState"_hs)
            .data<EAIAgentState::Disabled>("Disabled")
            .data<EAIAgentState::Patrol>("Patrol")
            .data<EAIAgentState::Chase>("Chase")
            .data<EAIAgentState::Evade>("Evade")
            .data<EAIAgentState::Attack>("Attack");

        entt::meta_factory<EBehaviorDebugState>()
            .type("EBehaviorDebugState"_hs)
            .data<EBehaviorDebugState::Inactive>("Inactive")
            .data<EBehaviorDebugState::Failure>("Failure")
            .data<EBehaviorDebugState::Success>("Success")
            .data<EBehaviorDebugState::Running>("Running");

        entt::meta_factory<AIAgentComponent>()
            .type("AIAgentComponent"_hs)
            .data<&AIAgentComponent::SetEnabled, &AIAgentComponent::GetEnabled>("Enabled")
                .custom<PropertyMeta>(PropertyPresets::Editable("Enabled", "AI",
                                                                "Whether the runtime AI logic is enabled"))
            .data<nullptr, &AIAgentComponent::GetState>("State")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("State", "AI", "Current AI state"))
            .data<nullptr, &AIAgentComponent::GetDesiredState>("DesiredState")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Desired State", "AI",
                                                                "Current AI state requested by decision logic"))
            .data<nullptr, &AIAgentComponent::GetTargetVisible>("TargetVisible")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Target Visible", "AI",
                                                                "Whether the AI currently sees the target"))
            .data<nullptr, &AIAgentComponent::GetBehaviorRootStatus>("BehaviorRootStatus")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("BT Root", "AI", "Behavior tree root debug state"))
            .data<nullptr, &AIAgentComponent::GetBehaviorEvadeStatus>("BehaviorEvadeStatus")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("BT Evade", "AI", "Evade task debug state"))
            .data<nullptr, &AIAgentComponent::GetBehaviorAttackStatus>("BehaviorAttackStatus")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("BT Attack", "AI", "Attack task debug state"))
            .data<nullptr, &AIAgentComponent::GetBehaviorChaseStatus>("BehaviorChaseStatus")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("BT Chase", "AI", "Chase task debug state"))
            .data<nullptr, &AIAgentComponent::GetBehaviorPatrolStatus>("BehaviorPatrolStatus")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("BT Patrol", "AI", "Patrol task debug state"));
    }

    void AIAgentComponent::ResetRuntimeState(bool enabled, const glm::vec3& initialTargetPosition)
    {
        enabled_ = enabled;
        state_ = enabled ? EAIAgentState::Patrol : EAIAgentState::Disabled;
        desiredState_ = state_;
        previousState_ = EAIAgentState::Disabled;
        targetVisible_ = false;
        patrolPoints.clear();
        moveDir = glm::vec3(0.0f);
        lookDir = glm::vec3(0.0f, 0.0f, 1.0f);
        lastKnownTargetPosition = initialTargetPosition;
        yaw = 0.0f;
        fireCooldownRemaining = 0.0f;
        targetMemoryRemaining = 0.0f;
        patrolPauseRemaining = 0.0f;
        targetVisibleGraceRemaining = 0.0f;
        stateHoldRemaining = 0.0f;
        strafeSign = 1.0f;
        patrolIndex = 0;
        triggerJump = false;
        patrolReachableFound = false;
        patrolUsedNearFallback = false;
        patrolRequestedIndex = 0;
        patrolSelectedIndex = 0;
        patrolCandidatesTested = 0;
        patrolWaypointCount = 0;
        patrolSelectionMs = 0.0f;
        patrolSelectedDistance = 0.0f;
        patrolStuckTime = 0.0f;
        patrolAbandonedTarget = false;
        patrolLastCommittedIndex = std::numeric_limits<size_t>::max();
        patrolSelectedTarget = glm::vec3(0.0f);
        patrolProgressAnchor = glm::vec3(0.0f);
        pathFollower.Clear();
        ResetDebugState();
    }

    void AIAgentComponent::ResetDebugState()
    {
        behaviorRootStatus_ = EBehaviorDebugState::Inactive;
        behaviorEvadeStatus_ = EBehaviorDebugState::Inactive;
        behaviorAttackStatus_ = EBehaviorDebugState::Inactive;
        behaviorChaseStatus_ = EBehaviorDebugState::Inactive;
        behaviorPatrolStatus_ = EBehaviorDebugState::Inactive;
    }
}
