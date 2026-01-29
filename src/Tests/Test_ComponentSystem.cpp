#include <catch2/catch_all.hpp>
#include "Assets/Node.h"
#include "Assets/Component.h"
#include <entt/meta/meta.hpp>
#include <entt/meta/resolve.hpp>
#include <memory>

// Define a concrete component for testing
class TestComponent : public Assets::Component {
public:
    TestComponent() = default;
    int value = 0;
    
    std::string_view GetTypeName() const override { return "TestComponent"; }
    entt::meta_type GetMetaType() const override { return entt::resolve<TestComponent>(); }
};

TEST_CASE("Component System Basics", "[Unit][Component]") {
    // CreateNode(name, translation, rotation, scale, instanceId)
    auto node = Assets::Node::CreateNode("TestNode", glm::vec3(0), glm::quat(1,0,0,0), glm::vec3(1), 0);
    
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
