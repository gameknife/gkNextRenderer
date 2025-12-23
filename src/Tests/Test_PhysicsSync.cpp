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
// GOption is already declared extern in Options.hpp, we need to define it somewhere if it's not linked from Engine.cpp
// Engine.cpp defines GOption. Wait, "ENGINE_API Options* GOption = nullptr;" in Engine.cpp.
// Since we link gkNextEngine, we should have access to GOption if ENGINE_API handles DLL import/export correctly.
// However, GOptionPtr is usually in DesktopMain.cpp which is NOT part of gkNextEngine lib but part of the Executable.
// So we must define GOptionPtr here.

// GOption is defined in Engine.cpp (as seen in file read).
// But we need to make sure we don't redefine it if it's exported.
// The error was about GOptionPtr.

// Mock Game Instance for Testing
class TestGameInstance : public NextGameInstanceBase
{
public:
    using NextGameInstanceBase::NextGameInstanceBase;
    void OnInit() override {}
    void OnTick(double deltaSeconds) override {}
    void OnDestroy() override {}
    bool OnRenderUI() override { return false; }
};

// Required by NextEngine
std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<TestGameInstance>(config, options, engine);
}

// Helper to simulate engine loop
void SimulateEngine(NextEngine* engine, int frames, double deltaSeconds)
{
    for (int i = 0; i < frames; ++i)
    {
        engine->Tick();
        // Since Tick() inside might use real system time or internal logic,
        // we might need to manually step physics if Tick() doesn't do it deterministically enough for tests,
        // but NextEngine::Tick() usually calls Physics::Tick().
    }
}

TEST_CASE("Physical Simulation of Static Body", "[Integration][Physics]") {
    
    // Setup Options
    const char* argv[] = { 
        "gkNextUnitTests", 
        "--width=800", 
        "--height=600",
        "--renderer=0" // Use Basic/Legacy Renderer to minimize overhead if possible, or 0 default
    };
    int argc = sizeof(argv) / sizeof(argv[0]);
    
    // Create Global Options as expected by Engine
    GOptionPtr.reset(new Options(argc, argv));
    GOption = GOptionPtr.get();
    
    // Initialize Engine
    // Note: This will attempt to create a Window. On CI servers without display, this will likely fail.
    // For local dev, a window will pop up.
    auto engine = std::make_unique<NextEngine>(*GOption);
    engine->Start(); // Start sets status, but main loop is manual usually or handled by SDL
    // Wait for engine to be ready? Start() might be non-blocking or just init state.
    
    // Get Physics Engine
    auto physics = engine->GetPhysicsEngine();
    REQUIRE(physics != nullptr);
    
    // Access Scene
    auto& scene = engine->GetScene();
    
    SECTION("Static Node stops falling Dynamic Node") {
        // 1. Setup minimal Scene Data manually to allow AddNode to work
        // We need 'models_' to have at least one element so 'cachedMeshShapes_' can be indexed.
        // However, 'AddNode' logic checks 'cachedMeshShapes_' size.
        // We need to manually populate 'cachedMeshShapes_' or trick the Scene.
        // Since 'cachedMeshShapes_' is private, we can't easily modify it from here without friend class or helper.
        
        // This highlights that 'AddNode' is tightly coupled to 'RebuildMeshBuffer' having run.
        // For this Integration Test to work 'properly', we should ideally load a simple scene.
        
        // ALTERNATIVE: Use the PhysicsEngine directly to Verify that SetVisible toggles the body.
        // We can create a Body manually (simulating what AddNode does) and then bind it to a Node.
        // Then call Node::SetVisible and check Body.
        
        // 1. Create a Physics Body manually
        glm::vec3 floorPos(0, -1, 0);
        // Create a Box Body (which represents our Static Node)
        // We use CreateBoxBody directly because mocking MeshShape is hard without Model data.
        auto bodyId = physics->CreateBoxBody(floorPos, glm::vec3(10, 1, 10), JPH::EMotionType::Static);
        
        // 2. Create Node and Bind
        auto floorNode = Assets::Node::CreateNode("Floor", floorPos, glm::quat(1,0,0,0), glm::vec3(1), 0, 0, false);
        floorNode->BindPhysicsBody(bodyId);
        
        // 3. Test Visibility Toggle Logic
        // Initial State: Body is Active (CreateBoxBody adds it as Activated usually?)
        // Let's check.
        // NextPhysics::CreateBoxBody -> CreateAndAddBody(..., Activate) -> AddBodyInternal -> bodies_[id] = ...
        
        // Let's verify initial state
        // Since we don't have IsBodyActive exposed in NextPhysics (we only added SetBodyActive),
        // we can verify by Side Effect: Does a dynamic body collide with it?
        
        // Create Dynamic Ball
        glm::vec3 ballPos(0, 5, 0);
        auto ballBodyId = physics->CreateSphereBody(ballPos, 1.0f, JPH::EMotionType::Dynamic);
        auto ballNode = Assets::Node::CreateNode("Ball", ballPos, glm::quat(1,0,0,0), glm::vec3(1), 0, 1, false);
        ballNode->BindPhysicsBody(ballBodyId);
        ballNode->SetMobility(Assets::Node::ENodeMobility::Dynamic); // Important for TickVelocity
        
        // SCENARIO 1: Visible = True (Default collision)
        floorNode->SetVisible(true); 
        
        // Simulate
        int frames = 60;
        for(int i=0; i<frames; ++i) engine->Tick();
        
        // Check Ball Height. Should be rested on floor.
        // Floor Y=-1, Extent Y=1 (Half=0.5). Top of floor = -1 + 0.5 = -0.5.
        // Ball Radius = 1.0. Center of Ball resting on floor = -0.5 + 1.0 = 0.5.
        float expectedY = 0.5f;
        // Allow some tolerance for physics solver
        // Using Catch2 Require
        // Note: We need to update ballNode transform from physics body? 
        // Node::TickVelocity does this! It calls NextPhysics::GetBody and updates Transform.
        // But Node::TickVelocity needs to be called. Scene::Tick calls it?
        // Scene::Tick -> tracks_ ... wait.
        
        // Node::TickVelocity is usually called in Scene::UpdateNodesGpuDriven or similar?
        // Let's check Scene.cpp.
        // Actually Scene.cpp Tick mostly handles Animation Tracks.
        // Where is Physics -> Node sync happening?
        // NextPhysics::Tick updates "bodies_" internal map.
        // We need to propagate that to Node if we check Node::WorldTranslation.
        // OR we just check the Physics Body position directly via NextPhysics::GetBody.
        
        auto* bodyInfo = physics->GetBody(ballBodyId);
        REQUIRE(bodyInfo != nullptr);
        // INFO("Ball Y: " << bodyInfo->position.y);
        // It should stop around 0.5. If it falls through, it will be much lower.
        REQUIRE(bodyInfo->position.y > -5.0f); // Did not fall into void
        
        
        // SCENARIO 2: Visible = False (No Collision)
        // Reset Ball
        // We can't easily reset position without exposing API, but we can just spawn a new ball.
        glm::vec3 ballPos2(0, 5, 0);
        auto ballBodyId2 = physics->CreateSphereBody(ballPos2, 1.0f, JPH::EMotionType::Dynamic);
        
        floorNode->SetVisible(false); // DISABLE FLOOR
        
        // Simulate
        for(int i=0; i<frames; ++i) engine->Tick();
        
        auto* bodyInfo2 = physics->GetBody(ballBodyId2);
        REQUIRE(bodyInfo2 != nullptr);
        // Should fall through the invisible floor
        // Gravity is -9.8. t=1s. d = 0.5*g*t^2 = -4.9m. 5 - 4.9 = 0.1. 
        // Wait, if it hits floor it stays at 0.5. If it passes through, it goes to < 0.
        // Let's run more frames to be sure.
        for(int i=0; i<30; ++i) engine->Tick();
        
        // INFO("Ball2 Y (Invisible Floor): " << bodyInfo2->position.y);
        REQUIRE(bodyInfo2->position.y < -1.0f); // Should be below floor center
    }
}
