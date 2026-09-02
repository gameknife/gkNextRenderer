#include <catch2/catch_all.hpp>

#include "TestCommon.hpp"

#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Gameplay/Character/NextCharacterController.h"

#include <glm/glm.hpp>

// ============================================================================
// Test_CharacterGroundVelocity.cpp - NextCharacterController::UpdateWithVelocity
// (see docs/projects/nextastrobot/nextastrobot-design.md §6.2). The platformer
// requirement is that a character standing on a kinematic platform rides along
// with it; without inheritGround the same platform slides out from under them.
// ============================================================================

namespace
{
    constexpr float kStep = 1.0f / 60.0f;
    constexpr float kGravity = 20.0f;
    constexpr float kPlatformSpeed = 2.0f;
    constexpr int kStepsPerSecond = 60;
    // NextPhysics::CreateBoxBody takes the full extent, so a 1 m tall slab is 0.5 m thick.
    constexpr float kSlabThickness = 1.0f;

    FCharacterControllerSettings AstroLikeSettings(const glm::vec3& footPosition)
    {
        FCharacterControllerSettings settings;
        settings.height = 1.5f;
        settings.radius = 0.35f;
        settings.maxStepHeight = 0.55f;
        settings.maxSlopeAngle = 50.0f;
        settings.initialPosition = footPosition;
        return settings;
    }

    // Falls the character onto whatever is below it, integrating gravity the way the
    // game does. Returns true if it ended the drop supported. Physics is stepped by a
    // direct fixed-rate Tick: the fixture's frame loop would feed sub-millisecond
    // wall-clock deltas that never accumulate to a fixed step, so kinematic bodies
    // would never actually move.
    bool SettleOnGround(NextPhysics& physics, NextCharacterController& controller, int steps)
    {
        float verticalVelocity = 0.0f;
        for (int i = 0; i < steps; ++i)
        {
            verticalVelocity = controller.IsOnGround() ? -1.0f : verticalVelocity - kGravity * kStep;
            controller.UpdateWithVelocity(glm::vec3(0.0f, verticalVelocity, 0.0f), true, kStep);
            physics.Tick(kStep);
        }
        return controller.IsOnGround();
    }

    // Drives one second of "stand still on a moving kinematic slab" and reports how
    // far the character travelled horizontally.
    float RidePlatformDistance(NextPhysics& physics, bool inheritGround, const glm::vec3& platformCenter)
    {
        const NextBodyID platform =
            physics.CreateBoxBody(platformCenter, glm::vec3(8.0f, kSlabThickness, 8.0f), NextMotionType::Kinematic);
        REQUIRE_FALSE(platform.IsInvalid());

        const float slabTop = platformCenter.y + 0.5f * kSlabThickness;
        NextCharacterController controller;
        controller.Create(&physics, AstroLikeSettings(glm::vec3(platformCenter.x, slabTop + 0.3f, platformCenter.z)));
        REQUIRE(controller.IsValid());
        REQUIRE(SettleOnGround(physics, controller, 40));

        const glm::vec3 start = controller.GetPosition();
        glm::vec3 platformPosition = platformCenter;
        for (int i = 0; i < kStepsPerSecond; ++i)
        {
            platformPosition.x += kPlatformSpeed * kStep;
            physics.MoveKinematicBody(platform, platformPosition, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), kStep);
            // The game integrates gravity itself; a grounded character keeps a small downward
            // bias so ExtendedUpdate's stick-to-floor keeps contact.
            controller.UpdateWithVelocity(glm::vec3(0.0f, -1.0f, 0.0f), inheritGround, kStep);
            physics.Tick(kStep);
        }
        const glm::vec3 end = controller.GetPosition();

        controller.Destroy();
        physics.RemoveBody(platform);
        return std::abs(end.x - start.x);
    }
}

TEST_CASE_METHOD(EngineTestFixture, "Character inherits kinematic platform velocity",
                 "[GPU][Integration][Physics][Gameplay]")
{
    NextPhysics* physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    // A 2 m/s platform carries the character roughly 2 m in one second. Contact
    // resolution costs a little, so the bar is 1.8 m.
    const float carried = RidePlatformDistance(*physics, true, glm::vec3(0.0f, 2.0f, 0.0f));
    CHECK(carried >= 1.8f);

    // Same setup without inheritGround: the slab slides away underneath, so the
    // character barely moves.
    const float left = RidePlatformDistance(*physics, false, glm::vec3(0.0f, 2.0f, 60.0f));
    CHECK(left < 0.2f);
}

TEST_CASE_METHOD(EngineTestFixture, "Character velocity update integrates a game-authored jump arc",
                 "[GPU][Integration][Physics][Gameplay]")
{
    NextPhysics* physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    const glm::vec3 floorCenter(0.0f, 0.0f, 120.0f);
    const NextBodyID floor =
        physics->CreateBoxBody(floorCenter, glm::vec3(20.0f, 2.0f, 20.0f), NextMotionType::Static);
    REQUIRE_FALSE(floor.IsInvalid());

    NextCharacterController controller;
    controller.Create(physics, AstroLikeSettings(glm::vec3(0.0f, 1.3f, 120.0f)));
    REQUIRE(controller.IsValid());
    REQUIRE(SettleOnGround(*physics, controller, 40));

    const float groundY = controller.GetPosition().y;

    // NextAstrobot's numbers: 8.9 m/s launch under 20 m/s^2 gravity peaks at ~2 m.
    float verticalVelocity = 8.9f;
    float peakY = groundY;
    for (int i = 0; i < 40; ++i)
    {
        controller.UpdateWithVelocity(glm::vec3(0.0f, verticalVelocity, 0.0f), true, kStep);
        physics->Tick(kStep);
        verticalVelocity -= kGravity * kStep;
        peakY = std::max(peakY, controller.GetPosition().y);
    }
    CHECK(peakY - groundY >= 1.7f);
    CHECK(peakY - groundY <= 2.4f);

    controller.Destroy();
    physics->RemoveBody(floor);
}
