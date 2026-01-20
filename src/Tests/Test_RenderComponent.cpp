#include <catch2/catch_all.hpp>
#include "Assets/Node.h"
#include "Runtime/Components/RenderComponent.h"
#include <memory>
#include <array>

TEST_CASE("RenderComponent Usage", "[Unit][RenderComponent]") {
    auto node = Assets::Node::CreateNode("RenderNode", glm::vec3(0), glm::quat(1,0,0,0), glm::vec3(1), 0);
    
    SECTION("Basic Properties") {
        auto renderComp = std::make_shared<Runtime::RenderComponent>();
        
        renderComp->SetModelId(123);
        renderComp->SetVisible(true);
        renderComp->SetRayCastVisible(false);
        
        std::array<uint32_t, 16> mats;
        mats.fill(7);
        renderComp->SetMaterial(mats);
        
        node->AddComponent(renderComp);
        
        auto retrieved = node->GetComponent<Runtime::RenderComponent>();
        REQUIRE(retrieved != nullptr);
        CHECK(retrieved->GetModelId() == 123);
        CHECK(retrieved->IsVisible() == true);
        CHECK(retrieved->IsRayCastVisible() == false);
        CHECK(retrieved->Materials()[0] == 7);
    }
}
