#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Gameplay/Camera/ModelViewController.hpp"
#include "Common/BenchMark.hpp"

#include <map>

class BenchmarkGameInstance : public NextGameInstanceBase
{
public:
    BenchmarkGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~BenchmarkGameInstance() override = default;

    // overrides
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {};
    void OnSceneLoaded() override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;

    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;
    
    bool OnRenderUI() override;
    
private:
    struct FBenchmarkRun
    {
        std::string scene;
        std::string label;
        std::string rendererName;
        std::map<std::string, std::string> cvars;
    };

    void LoadConfig(Runtime::Config::Options& options, Vulkan::WindowConfig& config);
    void BuildDefaultRuns(const Runtime::Config::Options& options);
    void ApplyCurrentRunSettings();
    void LoadCurrentRun();
    bool AdvanceRun();

    std::unique_ptr<BenchMarker> benchMarker_;
    Runtime::Camera::ModelViewController modelViewController_;
    FBenchmarkSettings benchmarkSettings_{};
    std::vector<FBenchmarkRun> benchmarkRuns_;
    std::map<std::string, std::string> defaultCvars_;
    size_t currentRunIndex_ = 0;

    double totalTime_ = 0.0;
};
