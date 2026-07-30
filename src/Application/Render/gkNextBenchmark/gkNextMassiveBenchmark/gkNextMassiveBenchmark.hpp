#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"

class BenchMarker;

class MassiveBenchmarkGameInstance final : public NextGameInstanceBase
{
public:
    MassiveBenchmarkGameInstance(
        Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);

    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    void OnInit() override;
    void OnDestroy() override {}
    void OnTick(double deltaSeconds) override;
    void OnSceneLoaded() override;
    bool OnRenderUI() override;

private:
    std::unique_ptr<BenchMarker> benchMarker_;
    uint32_t framesSinceLoad_ = 0;
    uint32_t maxVisibleCount_ = 0;
    bool observedWideVisibility_ = false;
};
