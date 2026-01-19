#include <catch2/catch_all.hpp>
#include "TestCommon.hpp"
#include "Runtime/NextPhysics.h"
#include "Assets/Node.h"

// Note: TestCommon.hpp provides EngineTestFixture and the necessary CreateGameInstance implementation

TEST_CASE_METHOD(EngineTestFixture, "Physical Simulation of Static Body Visibility", "[Integration][Physics]") {
    
    // Engine is already started by Fixture constructor
    // engine_ and options_ are available
    
    auto physics = engine_->GetPhysicsEngine();
    REQUIRE(physics != nullptr);
    
    SECTION("Static Node Collision Toggles with Visibility") {
        // 1. Create Static Floor
        // Using CreateBoxBody directly to bypass MeshShape dependency in unit test environment
        glm::vec3 floorPos(0, -1, 0);
        auto bodyId = physics->CreateBoxBody(floorPos, glm::vec3(10, 1, 10), NextMotionType::Static);
        
        auto floorNode = Assets::Node::CreateNode("Floor", floorPos, glm::quat(1,0,0,0), glm::vec3(1), 0, 0, false);
        floorNode->BindPhysicsBody(bodyId);
        
        // SCENARIO 1: Visible = True (Default collision)
        {
            floorNode->SetVisible(true);
            
            glm::vec3 ballPos(0, 5, 0);
            auto ballBodyId = physics->CreateSphereBody(ballPos, 1.0f, NextMotionType::Dynamic);
            
            Simulate(120);
            
            auto* bodyInfo = physics->GetBody(ballBodyId);
            REQUIRE(bodyInfo != nullptr);
            // Ball should rest on floor (approx Y = 0.5)
            CHECK(bodyInfo->position.y > -0.5f); 
            CHECK(bodyInfo->position.y < 1.0f);
        }

        // SCENARIO 2: Visible = False (No Collision)
        {
            floorNode->SetVisible(false);
            
            glm::vec3 ballPos(0, 5, 0);
            auto ballBodyId = physics->CreateSphereBody(ballPos, 1.0f, NextMotionType::Dynamic);
            
            Simulate(120);
            
            auto* bodyInfo = physics->GetBody(ballBodyId);
            REQUIRE(bodyInfo != nullptr);
            // Ball should fall through the invisible floor
            CHECK(bodyInfo->position.y < -1.0f);
        }
    }
}