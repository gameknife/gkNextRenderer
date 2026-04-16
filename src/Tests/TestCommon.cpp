#include "TestCommon.hpp"

#include <thread>
#include <vector>

// Define the global hook for creating the game instance.
// This is called by NextEngine internally.
std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<TestGameInstance>(config, options, engine);
}

EngineTestFixture::EngineTestFixture()
{
    // Mock arguments
    // Using a minimal resolution to speed up tests, renderer=0 (usually path tracer or default)
    const char* argv[] = { 
        "gkNextUnitTests", 
        "--width=800", 
        "--height=600",
        "--fastexit=false"
    };
    int argc = sizeof(argv) / sizeof(argv[0]);

    options_ = std::make_unique<Options>(argc, argv);
    GOption = options_.get();

    engine_ = std::make_unique<NextEngine>(*GOption);
    engine_->Start();
    
    // Warm up a bit to ensure resources are initialized
    Simulate(10);
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    Simulate(10);
}

EngineTestFixture::~EngineTestFixture()
{
    if (engine_)
    {
        engine_->End();
        engine_.reset();
    }
    // Clear the global pointer
    GOption = nullptr;
    options_.reset();
}

void EngineTestFixture::Simulate(int frames)
{
    if (!engine_) return;
    for (int i = 0; i < frames; ++i)
    {
        engine_->Tick(true);
    }
}
