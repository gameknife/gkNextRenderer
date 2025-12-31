#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include "Runtime/Engine.hpp"
#include "Assets/Scene.hpp"
#include "Assets/Node.h"
#include "Runtime/NextPhysics.h"
#include "Options.hpp"
#include <memory>
#include <thread>

std::unique_ptr<Options> GOptionPtr;

class TestGameInstance : public NextGameInstanceBase
{
public:
    using NextGameInstanceBase::NextGameInstanceBase;
    void OnInit() override {}
    void OnTick(double deltaSeconds) override {}
    void OnDestroy() override {}
    bool OnRenderUI() override { return false; }
};

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<TestGameInstance>(config, options, engine);
}

void SimulateEngine(NextEngine* engine, int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        engine->Tick();
    }
}

TEST_CASE("Physical Simulation of Static Body Visibility", "[Integration][Physics]") {
    
    const char* argv[] = { 
        "gkNextUnitTests", 
        "--width=800", 
        "--height=600",
        "--renderer=0" 
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    GOptionPtr.reset(new Options(argc, argv));
    GOption = GOptionPtr.get();
    
    auto engine = std::make_unique<NextEngine>(*GOption);
    engine->Start();
    
    // Warm up
    SimulateEngine(engine.get(), 10);
    
    auto physics = engine->GetPhysicsEngine();
    REQUIRE(physics != nullptr);
    
    SECTION("Static Node Collision Toggles with Visibility") {
        // 1. Create Static Floor
        // Using CreateBoxBody directly to bypass MeshShape dependency in unit test environment
        glm::vec3 floorPos(0, -1, 0);
        auto bodyId = physics->CreateBoxBody(floorPos, glm::vec3(10, 1, 10), JPH::EMotionType::Static);
        
        auto floorNode = Assets::Node::CreateNode("Floor", floorPos, glm::quat(1,0,0,0), glm::vec3(1), 0, 0, false);
        floorNode->BindPhysicsBody(bodyId);
        
        // SCENARIO 1: Visible = True (Default collision)
        {
            floorNode->SetVisible(true);
            
            glm::vec3 ballPos(0, 5, 0);
            auto ballBodyId = physics->CreateSphereBody(ballPos, 1.0f, JPH::EMotionType::Dynamic);
            
            SimulateEngine(engine.get(), 60);
            
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
            auto ballBodyId = physics->CreateSphereBody(ballPos, 1.0f, JPH::EMotionType::Dynamic);
            
            SimulateEngine(engine.get(), 60);
            
            auto* bodyInfo = physics->GetBody(ballBodyId);
            REQUIRE(bodyInfo != nullptr);
            // Ball should fall through the invisible floor
            CHECK(bodyInfo->position.y < -1.0f);
        }
    }
    
    engine->End();
    engine.reset();
}
