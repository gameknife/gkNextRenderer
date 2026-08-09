#include <catch2/catch_all.hpp>
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include <memory>

TEST_CASE("PhysicsComponent Usage", "[Unit][PhysicsComponent]") {
    auto node = Assets::Node::CreateNode("PhysicsNode", glm::vec3(0), glm::quat(1,0,0,0), glm::vec3(1), 0);
    
    SECTION("Basic Properties") {
        auto physComp = std::make_shared<Runtime::PhysicsComponent>();
        
        NextBodyID bodyId(123); // Mock body ID
        
        physComp->BindPhysicsBody(bodyId);
        physComp->SetMobility(Runtime::ENodeMobility::Dynamic);
        physComp->SetPhysicsOffset(glm::vec3(0, 1, 0));
        
        node->AddComponent(physComp);
        
        auto retrieved = node->GetComponent<Runtime::PhysicsComponent>();
        REQUIRE(retrieved != nullptr);
        CHECK(retrieved->GetPhysicsBody().GetIndex() == 123);
        CHECK(retrieved->GetMobility() == Runtime::ENodeMobility::Dynamic);
        CHECK(retrieved->GetPhysicsOffset().y == 1.0f);
    }
}
