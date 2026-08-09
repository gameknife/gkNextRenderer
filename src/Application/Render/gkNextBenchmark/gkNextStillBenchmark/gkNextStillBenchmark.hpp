#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"

class BenchMarker;

class BenchmarkGameInstance : public NextGameInstanceBase
{
public:
    BenchmarkGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~BenchmarkGameInstance() override = default;

    // overrides
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    void OnSceneLoaded() override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    
    bool OnRenderUI() override;
    
private:
    std::unique_ptr<BenchMarker> benchMarker_;
    std::vector<std::string> demoScenes_;
    size_t currentSceneIndex_ = 0;
    bool previousTickAnimation_ = true;
    bool previousTickPhysics_ = true;
    bool tickSettingsOverridden_ = false;
};
