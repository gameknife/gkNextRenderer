#include <catch2/catch_all.hpp>
#include "TestCommon.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>

// Note: TestCommon.hpp provides EngineTestFixture and the necessary CreateGameInstance implementation

TEST_CASE_METHOD(EngineTestFixture, "Physical Simulation of Static Body Visibility", "[Integration][Physics]") {
    
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

TEST_CASE_METHOD(EngineTestFixture, "Dynamic Physics Offset Uses Local Space", "[Integration][Physics]") {
    auto physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);

    glm::vec3 meshTranslation(1.25f, 2.5f, -0.75f);
    glm::quat meshRotation = glm::normalize(glm::angleAxis(glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 meshScale(1.0f, 1.0f, 1.0f);
    glm::vec3 localOffset(0.18f, 0.0f, -0.06f);
    glm::vec3 bodyExtent(0.4f, 0.2f, 0.3f);
    glm::vec3 bodyPosition = meshTranslation + meshRotation * (localOffset * meshScale);

    auto bodyId = physics->CreateBoxBody(bodyPosition, meshRotation, bodyExtent, NextMotionType::Dynamic);
    auto node = Assets::Node::CreateNode("DynamicOffsetNode", meshTranslation, meshRotation, meshScale, 1);

    auto physComp = std::make_shared<Runtime::PhysicsComponent>();
    physComp->BindPhysicsBody(bodyId);
    physComp->SetMobility(Runtime::ENodeMobility::Dynamic);
    physComp->SetPhysicsOffset(localOffset);
    node->AddComponent(physComp);

    glm::mat4 combined(1.0f);
    node->TickVelocity(combined);
    CHECK(glm::all(glm::epsilonEqual(node->WorldTranslation(), meshTranslation, 0.0001f)));
    CHECK(glm::abs(glm::dot(node->WorldRotation(), meshRotation)) > 0.9999f);

    physics->RemoveBody(bodyId);
}

TEST_CASE_METHOD(EngineTestFixture, "SetBodyVelocity Reactivates Resting Dynamic Body", "[Integration][Physics]") {
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
