#include <catch2/catch_all.hpp>
#include "Assets/Node.h"
#include "Assets/Component.h"
#include <memory>

// Define a concrete component for testing
class TestComponent : public Assets::Component {
public:
    TestComponent() = default;
    int value = 0;
};

TEST_CASE("Component System Basics", "[Unit][Component]") {
    // CreateNode(name, translation, rotation, scale, modelId, instanceId, replace)
    auto node = Assets::Node::CreateNode("TestNode", glm::vec3(0), glm::quat(1,0,0,0), glm::vec3(1), 0, 0, false);
    
    SECTION("Add and Get Component") {
        auto comp = std::make_shared<TestComponent>();
        comp->value = 42;
        
        node->AddComponent(comp);
        
        auto retrieved = node->GetComponent<TestComponent>();
        REQUIRE(retrieved != nullptr);
        CHECK(retrieved->value == 42);
    }
    
    SECTION("Uniqueness Constraint") {
        auto comp1 = std::make_shared<TestComponent>();
        comp1->value = 1;
        node->AddComponent(comp1);
        
        auto comp2 = std::make_shared<TestComponent>();
        comp2->value = 2;
        node->AddComponent(comp2); // Should replace
        
        auto retrieved = node->GetComponent<TestComponent>();
        REQUIRE(retrieved != nullptr);
        CHECK(retrieved->value == 2);
        CHECK(retrieved == comp2);
    }

    SECTION("Get Non-existent Component") {
        auto retrieved = node->GetComponent<TestComponent>();
        CHECK(retrieved == nullptr);
    }
}
