#include <catch2/catch_all.hpp>
#include "TestCommon.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Gameplay/Character/NextCharacterController.h"
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>

// Note: TestCommon.hpp provides EngineTestFixture and the necessary CreateGameInstance implementation

TEST_CASE_METHOD(EngineTestFixture, "Physical Simulation of Static Body Visibility", "[GPU][Integration][Physics]") {
    
    // Engine is already started by Fixture constructor
    // engine_ and options_ are available
    
    auto physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);
    
    SECTION("Static Node Collision Toggles") {
        // 1. Create Static Floor
        // Using CreateBoxBody directly to bypass MeshShape dependency in unit test environment
        glm::vec3 floorPos(0, -1, 0);
        auto bodyId = physics->CreateBoxBody(floorPos, glm::vec3(10, 1, 10), NextMotionType::Static);
        
        auto floorNode = Assets::Node::CreateNode("Floor", floorPos, glm::quat(1,0,0,0), glm::vec3(1), 0);
        
        // Setup Physics
        auto physComp = std::make_shared<Runtime::PhysicsComponent>();
        physComp->BindPhysicsBody(bodyId);
        floorNode->AddComponent(physComp);
        
        // Setup RenderComponent (though not strictly needed for physics test, good for completeness)
        auto renderComp = std::make_shared<Runtime::RenderComponent>();
        floorNode->AddComponent(renderComp);

        // SCENARIO 1: Body Active (Default collision)
        {
            physics->SetBodyActive(bodyId, true);
            renderComp->SetVisible(true); // Visual only now
            
            glm::vec3 ballPos(0, 5, 0);
            auto ballBodyId = physics->CreateSphereBody(ballPos, 1.0f, NextMotionType::Dynamic);
            
            Simulate(120);
            
            auto* bodyInfo = physics->GetBody(ballBodyId);
            REQUIRE(bodyInfo != nullptr);
            // Ball should rest on floor (approx Y = 0.5)
            CHECK(bodyInfo->position.y > -0.5f); 
            CHECK(bodyInfo->position.y < 1.0f);
            physics->RemoveBody(ballBodyId);
        }

        // SCENARIO 2: Body Inactive (No Collision)
        {
            physics->SetBodyActive(bodyId, false);
            renderComp->SetVisible(false);
            
            glm::vec3 ballPos(0, 5, 0);
            auto ballBodyId = physics->CreateSphereBody(ballPos, 1.0f, NextMotionType::Dynamic);
            
            Simulate(120);
            
            auto* bodyInfo = physics->GetBody(ballBodyId);
            REQUIRE(bodyInfo != nullptr);
            // Ball should fall through the invisible floor
            CHECK(bodyInfo->position.y < -1.0f);
            physics->RemoveBody(ballBodyId);
        }
        
        physics->RemoveBody(bodyId);
    }
}

TEST_CASE_METHOD(EngineTestFixture, "Dynamic Physics Offset Uses Local Space", "[GPU][Integration][Physics]") {
    auto physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    glm::vec3 meshTranslation(1.25f, 2.5f, -0.75f);
    glm::quat meshRotation = glm::normalize(glm::angleAxis(glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 meshScale(1.0f, 1.0f, 1.0f);
    glm::vec3 localOffset(0.18f, 0.0f, -0.06f);
    glm::vec3 bodyExtent(0.4f, 0.2f, 0.3f);
    glm::vec3 bodyPosition = meshTranslation + meshRotation * (localOffset * meshScale);

    auto bodyId = physics->CreateBoxBody(bodyPosition, meshRotation, bodyExtent, NextMotionType::Dynamic);
    auto node = Assets::Node::CreateNode("DynamicOffsetNode", meshTranslation, meshRotation, meshScale,
                                         engine_->GetScene().GenerateInstanceId());

    auto physComp = std::make_shared<Runtime::PhysicsComponent>();
    physComp->BindPhysicsBody(bodyId);
    physComp->SetMobility(Runtime::ENodeMobility::Dynamic);
    physComp->SetPhysicsOffset(localOffset);
    node->AddComponent(physComp);

    engine_->GetScene().AddNode(node);
    physics->SetBodyTransform(bodyId, bodyPosition + glm::vec3(0.4f, 0.8f, -0.2f), meshRotation, true);
    Simulate(1);

    const FNextPhysicsBody* body = physics->GetBody(bodyId);
    REQUIRE(body != nullptr);
    const glm::vec3 expectedTranslation = body->position - body->rotation * (localOffset * meshScale);
    CHECK(glm::all(glm::epsilonEqual(node->WorldTranslation(), expectedTranslation, 0.0001f)));
    CHECK(glm::abs(glm::dot(node->WorldRotation(), meshRotation)) > 0.9999f);

    engine_->GetScene().RemoveNodeByInstanceId(node->GetInstanceId());
}

TEST_CASE_METHOD(EngineTestFixture, "SetBodyVelocity Reactivates Resting Dynamic Body", "[GPU][Integration][Physics]") {
    auto physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    auto floorBodyId = physics->CreateBoxBody(glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(8.0f, 1.0f, 8.0f), NextMotionType::Static);
    auto boxBodyId = physics->CreateBoxBody(glm::vec3(0.0f, 1.5f, 0.0f), glm::quat(1, 0, 0, 0), glm::vec3(0.4f, 0.4f, 0.4f), NextMotionType::Dynamic);

    Simulate(180);

    auto* restingBody = physics->GetBody(boxBodyId);
    REQUIRE(restingBody != nullptr);
    const float restingX = restingBody->position.x;

    physics->SetBodyVelocity(boxBodyId, glm::vec3(1.5f, 0.0f, 0.0f), glm::vec3(0.0f));
    Simulate(30);

    auto* movedBody = physics->GetBody(boxBodyId);
    REQUIRE(movedBody != nullptr);
    CHECK(movedBody->position.x > restingX + 0.05f);

    physics->RemoveBody(boxBodyId);
    physics->RemoveBody(floorBodyId);
}

TEST_CASE_METHOD(EngineTestFixture, "Physics catch-up advances the full fixed-step interval",
                 "[GPU][Integration][Physics][FixedStep]")
{
    auto* physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    physics->OnSceneStarted();
    const NextBodyID sphere =
        physics->CreateSphereBody({100.0f, 10.0f, 100.0f}, 0.25f, NextMotionType::Dynamic);

    physics->Tick(4.0 / 60.0);

    const FNextPhysicsBody* body = physics->GetBody(sphere);
    REQUIRE(body != nullptr);
    CHECK(body->position.y < 9.99f);
    CHECK(body->position.y > 9.9f);

    physics->RemoveBody(sphere);
}

TEST_CASE_METHOD(EngineTestFixture, "Async physics publishes only after completion",
                 "[GPU][Integration][Physics][Async]")
{
    auto* physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    const NextBodyID bodyId = physics->CreateSphereBody(
        glm::vec3(0.0f, 5.0f, 0.0f), 0.25f, NextMotionType::Dynamic);
    physics->SetBodyVelocity(bodyId, glm::vec3(6.0f, 0.0f, 0.0f), glm::vec3(0.0f));

    const FNextPhysicsBody* publishedBody = physics->GetBody(bodyId);
    REQUIRE(publishedBody != nullptr);
    const glm::vec3 publishedBefore = publishedBody->position;
    physics->KickTick(1.0 / 60.0);
    CHECK(physics->GetBody(bodyId)->position == publishedBefore);

    physics->CompleteTick();
    CHECK(physics->GetBody(bodyId)->position.x > publishedBefore.x);
    physics->RemoveBody(bodyId);
}

TEST_CASE_METHOD(EngineTestFixture, "High render rate keeps physics updates at 60 Hz",
                 "[GPU][Integration][Physics][FixedStep]")
{
    auto* physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    physics->OnSceneStarted();
    for (int frame = 0; frame < 240; ++frame)
    {
        physics->Tick(1.0 / 240.0);
    }

    const FNextPhysicsBodyStats stats = physics->GetBodyStats();
    CHECK(stats.updateCalls == 60);
    CHECK(stats.simulatedSteps == 60);
}

TEST_CASE_METHOD(EngineTestFixture, "Rolling sphere settles and enters sleep",
                 "[GPU][Integration][Physics][SphereSleep]")
{
    auto* physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    physics->OnSceneStarted();
    const NextBodyID floor =
        physics->CreateBoxBody({0.0f, -0.5f, 0.0f}, {100.0f, 1.0f, 100.0f}, NextMotionType::Static);
    const NextBodyID sphere =
        physics->CreateSphereBody({0.0f, 0.3f, 0.0f}, 0.25f, NextMotionType::Dynamic);
    physics->SetBodyVelocity(sphere, {2.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -8.0f});

    for (int step = 0; step < 600; ++step)
    {
        physics->Tick(1.0 / 60.0);
    }

    const FNextPhysicsDebugState state = physics->GetBodyDebugState(sphere);
    const FNextPhysicsBody* body = physics->GetBody(sphere);
    REQUIRE(state.isValid);
    REQUIRE(body != nullptr);
    CHECK_FALSE(state.isActive);
    CHECK(glm::length(body->velocity) < 0.05f);

    physics->RemoveBody(sphere);
    physics->RemoveBody(floor);
}

TEST_CASE_METHOD(EngineTestFixture, "Dense 3000 sphere pile retains ground contacts",
                 "[GPU][.stress][Integration][Physics][DensePile]")
{
    auto* physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    constexpr int countX = 16;
    constexpr int countZ = 16;
    constexpr int countY = 12;
    constexpr float radius = 0.12f;
    constexpr float spacing = radius * 2.01f;
    constexpr float layerHeight = spacing * 0.8164965809f;
    constexpr float rowDepth = spacing * 0.8660254038f;
    constexpr glm::vec3 pileOrigin(30.0f, 0.0f, 30.0f);

    physics->OnSceneStarted();
    const NextBodyID floor =
        physics->CreateBoxBody(pileOrigin + glm::vec3(2.0f, -0.5f, 2.0f),
                               {100.0f, 1.0f, 100.0f}, NextMotionType::Static);

    std::vector<NextBodyID> spheres;
    spheres.reserve(countX * countZ * countY);
    for (int y = 0; y < countY; ++y)
    {
        for (int z = 0; z < countZ; ++z)
        {
            for (int x = 0; x < countX; ++x)
            {
                const float layerOffsetX = (y & 1) != 0 ? spacing * 0.5f : 0.0f;
                const float layerOffsetZ = (y & 1) != 0 ? rowDepth / 3.0f : 0.0f;
                const float rowOffsetX = (z & 1) != 0 ? spacing * 0.5f : 0.0f;
                const glm::vec3 position =
                    pileOrigin + glm::vec3(
                        radius + x * spacing + rowOffsetX + layerOffsetX,
                        radius + y * layerHeight,
                        radius + z * rowDepth + layerOffsetZ);
                spheres.push_back(physics->CreateSphereBody(position, radius, NextMotionType::Dynamic));
            }
        }
    }
    REQUIRE(spheres.size() == 3072);

    for (int step = 0; step < 360; ++step)
    {
        physics->Tick(1.0 / 60.0);
    }

    float minimumY = std::numeric_limits<float>::max();
    for (const NextBodyID sphere : spheres)
    {
        const FNextPhysicsBody* body = physics->GetBody(sphere);
        REQUIRE(body != nullptr);
        minimumY = std::min(minimumY, body->position.y);
    }
    CHECK(minimumY > -radius);

    for (const NextBodyID sphere : spheres)
    {
        physics->RemoveBody(sphere);
    }
    physics->RemoveBody(floor);
}

TEST_CASE_METHOD(EngineTestFixture, "Character stance resize keeps feet anchored and checks headroom",
                 "[GPU][Integration][Physics][Character]")
{
    auto* physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    const NextBodyID floor =
        physics->CreateBoxBody({0.0f, -0.5f, 0.0f}, {5.0f, 0.5f, 5.0f}, NextMotionType::Static);

    FCharacterControllerSettings settings;
    settings.height = 1.8f;
    settings.radius = 0.3f;
    settings.initialPosition = {0.0f, 0.02f, 0.0f};

    NextCharacterController controller;
    controller.Create(physics, settings);
    REQUIRE(controller.IsValid());
    CHECK(controller.GetHeight() == Catch::Approx(1.8f));

    const glm::vec3 standingFeet = controller.GetPosition();
    REQUIRE(controller.TrySetHeight(1.0f));
    CHECK(controller.GetHeight() == Catch::Approx(1.0f));
    CHECK(glm::distance(controller.GetPosition(), standingFeet) < 1.0e-4f);
    CHECK_FALSE(controller.TrySetHeight(0.5f));
    CHECK(controller.GetHeight() == Catch::Approx(1.0f));

    const NextBodyID ceiling =
        physics->CreateBoxBody({0.0f, 1.5f, 0.0f}, {2.0f, 0.1f, 2.0f}, NextMotionType::Static);
    CHECK_FALSE(controller.TrySetHeight(1.8f));
    CHECK(controller.GetHeight() == Catch::Approx(1.0f));
    CHECK(glm::distance(controller.GetPosition(), standingFeet) < 1.0e-4f);

    physics->RemoveBody(ceiling);
    REQUIRE(controller.TrySetHeight(1.8f));
    CHECK(controller.GetHeight() == Catch::Approx(1.8f));
    CHECK(glm::distance(controller.GetPosition(), standingFeet) < 1.0e-4f);

    const glm::vec3 scriptedPosition = standingFeet + glm::vec3(0.75f, 0.0f, 0.5f);
    controller.SetPosition(scriptedPosition);
    CHECK(glm::distance(controller.GetPosition(), scriptedPosition) < 1.0e-4f);

    for (int frame = 0; frame < 30; ++frame)
    {
        controller.Update(glm::vec3(1.0f, 0.0f, 0.0f), 2.0f, false, 1.0f / 60.0f);
    }
    CHECK(controller.GetPosition().x > standingFeet.x + 0.5f);

    controller.Destroy();
    physics->RemoveBody(floor);
}

TEST_CASE_METHOD(EngineTestFixture, "Wheeled vehicle suspension and drivetrain telemetry", "[GPU][Integration][Physics][Vehicle]")
{
    auto* physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);
    const NextBodyID floor = physics->CreateBoxBody({0.0f, -0.5f, 0.0f}, {40.0f, 0.5f, 12.0f},
                                                    NextMotionType::Static);

    FNextVehicleSettings settings;
    settings.initialPosition = {0.0f, 1.4f, 0.0f};
    settings.centerOfMassOffset = {0.0f, -0.35f, 0.0f};
    settings.frontAntiRollStiffness = 10000.0f;
    settings.rearAntiRollStiffness = 14000.0f;
    settings.engine.maxTorque = 1150.0f;
    constexpr float axleX[] = {2.5f, -0.6f, -1.75f};
    for (int axle = 0; axle < 3; ++axle)
    {
        for (int side = 0; side < 2; ++side)
        {
            FNextWheelSettings wheel;
            wheel.position = {axleX[axle], -0.2f, side ? -1.0f : 1.0f};
            wheel.suspensionMin = 0.15f;
            wheel.suspensionMax = 0.55f;
            wheel.suspensionFrequency = axle == 0 ? 1.3f : 1.6f;
            wheel.suspensionDamping = axle == 0 ? 0.35f : 0.40f;
            wheel.steered = axle == 0;
            wheel.driven = axle != 0;
            settings.wheels.push_back(wheel);
        }
    }

    const NextVehicleID vehicle = physics->CreateWheeledVehicle(settings);
    REQUIRE(vehicle != invalidNextVehicleId);
    Simulate(45);
    physics->SetVehicleDiffLock(vehicle, true);
    physics->SetVehicleAllWheelDrive(vehicle, true);
    physics->SetVehicleInput(vehicle, {0.8f, 0.0f, 0.0f, 0.0f});
    Simulate(120);

    glm::vec3 position; glm::quat rotation;
    REQUIRE(physics->GetVehicleBodyTransform(vehicle, position, rotation));
    CHECK(position.x > 0.25f);
    const glm::vec3 up = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
    CHECK(glm::dot(up, glm::vec3(0.0f, 1.0f, 0.0f)) > 0.7f);

    FNextVehicleTelemetry telemetry;
    REQUIRE(physics->GetVehicleTelemetry(vehicle, telemetry));
    CHECK(telemetry.gear >= 1);
    CHECK(telemetry.rpm > 0.0f);
    CHECK(telemetry.wheelSlip.size() == 6);
    CHECK(telemetry.wheelContact.size() == 6);

    physics->RemoveVehicle(vehicle);
    physics->RemoveBody(floor);

    const glm::quat rampRotation = glm::angleAxis(glm::radians(10.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const NextBodyID ramp = physics->CreateBoxBody({0.0f, 3.0f, 0.0f}, rampRotation, {20.0f, 0.6f, 8.0f},
                                                   NextMotionType::Static);
    settings.initialPosition = {-7.0f, 2.10f, 0.0f};
    settings.initialRotation = rampRotation;
    const NextVehicleID climbingVehicle = physics->CreateWheeledVehicle(settings);
    REQUIRE(climbingVehicle != invalidNextVehicleId);
    physics->SetVehicleInput(climbingVehicle, {0.0f, 0.0f, 1.0f, 1.0f});
    Simulate(45);
    physics->SetVehicleAllWheelDrive(climbingVehicle, true);
    physics->SetVehicleDiffLock(climbingVehicle, true);
    physics->SetVehicleInput(climbingVehicle, {0.9f, 0.0f, 0.0f, 0.0f});
    Simulate(90);
    REQUIRE(physics->GetVehicleBodyTransform(climbingVehicle, position, rotation));
    CHECK(position.x > -6.5f);
    CHECK(glm::dot(rotation * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)) > 0.5f);
    physics->RemoveVehicle(climbingVehicle);
    physics->RemoveBody(ramp);
}
