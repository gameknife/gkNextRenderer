#pragma once
#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/ModelViewController.hpp"

class BenchMarker;

class BenchmarkGameInstance : public NextGameInstanceBase
{
public:
    BenchmarkGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~BenchmarkGameInstance() override = default;

    // overrides
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {};
    void OnSceneLoaded() override;

    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;
    
    bool OnRenderUI() override;
    
    // quick access engine
    NextEngine& GetEngine() { return *engine_; }
private:
    NextEngine* engine_;

    std::unique_ptr<BenchMarker> benchMarker_;
    ModelViewController modelViewController_;

    double totalTime_;
};
