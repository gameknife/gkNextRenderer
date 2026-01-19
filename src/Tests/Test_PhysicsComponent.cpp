#include <catch2/catch_all.hpp>
#include "Assets/Node.h"
#include "Assets/PhysicsComponent.h"
#include <memory>

TEST_CASE("PhysicsComponent Usage", "[Unit][PhysicsComponent]") {
    auto node = Assets::Node::CreateNode("PhysicsNode", glm::vec3(0), glm::quat(1,0,0,0), glm::vec3(1), 0);
    
    SECTION("Basic Properties") {
        auto physComp = std::make_shared<Assets::PhysicsComponent>();
        
        NextBodyID bodyId(123); // Mock body ID
        
        physComp->BindPhysicsBody(bodyId);
        physComp->SetMobility(Assets::ENodeMobility::Dynamic);
        physComp->SetPhysicsOffset(glm::vec3(0, 1, 0));
        
        node->AddComponent(physComp);
        
        auto retrieved = node->GetComponent<Assets::PhysicsComponent>();
        REQUIRE(retrieved != nullptr);
        CHECK(retrieved->GetPhysicsBody().GetIndex() == 123);
        CHECK(retrieved->GetMobility() == Assets::ENodeMobility::Dynamic);
        CHECK(retrieved->GetPhysicsOffset().y == 1.0f);
    }
}
