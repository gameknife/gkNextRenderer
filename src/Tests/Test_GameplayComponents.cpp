#include <catch2/catch_all.hpp>

#include "Engine/Assets/Core/Node.h"
#include "Engine/NextGameplay/Components/AIAgentComponent.h"
#include "Engine/NextGameplay/Components/CharacterAnimationComponent.h"
#include "Engine/NextGameplay/Components/CharacterControlComponent.h"
#include "Engine/NextGameplay/Components/CharacterGameplayComponent.h"

TEST_CASE("CharacterGameplayComponent stores runtime-facing gameplay flags", "[Unit][Gameplay]")
{
    auto node = Assets::Node::CreateNode("GameplayActor", glm::vec3(0.0f), glm::quat(1, 0, 0, 0), glm::vec3(1.0f), 1);
    auto component = std::make_shared<NextGameplay::CharacterGameplayComponent>();

    component->SetFirstPersonMode(true);
    component->SetFootIKEnabled(false);
    component->SetMovementMode(NextGameplay::ECharacterMovementMode::MoveAligned);
    component->SetEyeHeight(1.8f);
    component->modelLoadRequested = true;

    node->AddComponent(component);

    auto stored = node->GetComponent<NextGameplay::CharacterGameplayComponent>();
    REQUIRE(stored != nullptr);
    CHECK(stored->GetFirstPersonMode());
    CHECK_FALSE(stored->GetFootIKEnabled());
    CHECK(stored->GetMovementMode() == NextGameplay::ECharacterMovementMode::MoveAligned);
    CHECK(stored->GetEyeHeight() == Catch::Approx(1.8f));
    CHECK(stored->modelLoadRequested);
}

TEST_CASE("AIAgentComponent reset produces patrol-ready defaults", "[Unit][Gameplay]")
{
    NextGameplay::AIAgentComponent component;
    component.ResetRuntimeState(true, glm::vec3(3.0f, 0.0f, 4.0f));

    CHECK(component.GetEnabled());
    CHECK(component.GetState() == NextGameplay::EAIAgentState::Patrol);
    CHECK(component.GetDesiredState() == NextGameplay::EAIAgentState::Patrol);
    CHECK(component.lastKnownTargetPosition == glm::vec3(3.0f, 0.0f, 4.0f));
    CHECK(component.lookDir == glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(component.patrolPoints.empty());
    CHECK(component.pathFollower.waypoints.empty());
    CHECK(component.GetBehaviorRootStatus() == NextGameplay::EBehaviorDebugState::Inactive);
}

TEST_CASE("CharacterAnimationComponent maps default locomotion clips", "[Unit][Gameplay]")
{
    NextGameplay::CharacterAnimationComponent component;
    component.MapAnimationNames({"Idle_A", "Walking_A", "Running_A", "Walking_Backwards",
                                 "Running_Strafe_Left", "Running_Strafe_Right",
                                 "Jump_Start", "Jump_Idle", "Jump_Land"});

    CHECK(component.GetIdleAnimationName() == "Idle_A");
    component.ClearRuntimeState();
    CHECK(component.GetIdleAnimationName().empty());
    CHECK(component.GetAnimState() == NextGameplay::ECharacterAnimState::Idle);
}

TEST_CASE("CharacterControlComponent captures controller intent", "[Unit][Gameplay]")
{
    NextGameplay::CharacterControlComponent component;

    component.SetControlSource(NextGameplay::ECharacterControlSource::AI);
    component.SetMoveIntent(glm::vec3(1.0f, 0.0f, 0.0f));
    component.SetLookIntent(glm::vec3(0.0f, 0.0f, -1.0f));
    component.SetDesiredSpeed(6.0f);
    component.SetSprinting(true);
    component.SetJumpRequested(true);

    CHECK(component.GetControlSource() == NextGameplay::ECharacterControlSource::AI);
    CHECK(component.GetMoveIntent() == glm::vec3(1.0f, 0.0f, 0.0f));
    CHECK(component.GetLookIntent() == glm::vec3(0.0f, 0.0f, -1.0f));
    CHECK(component.GetDesiredSpeed() == Catch::Approx(6.0f));
    CHECK(component.GetSprinting());
    CHECK(component.ConsumeJumpRequested());
    CHECK_FALSE(component.GetJumpRequested());

    component.ClearFrameState();
    CHECK(component.GetMoveIntent() == glm::vec3(0.0f));
    CHECK(component.GetDesiredSpeed() == Catch::Approx(0.0f));
    CHECK_FALSE(component.GetSprinting());
}
