#include <catch2/catch_all.hpp>
#include "TestCommon.hpp"
#include "Runtime/Subsystems/NextPhysics.h"
#include "Assets/Core/Node.h"
#include "Runtime/Components/RenderComponent.h"

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
