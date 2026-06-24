#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"

#include <unordered_map>

struct VisualTestSceneConfig
{
    std::string path;
    int framesToWait = 60;
};

struct VisualTestResult
{
    std::string sceneName;
    std::string scenePath;
    std::string sceneCategory;
    std::string rendererName;
    std::string screenshotPath;
    std::string baselinePath;
    std::string diffImagePath;
    std::string baselineStatus;
    double renderTimeSeconds = 0.0;
    double baselineRmse = 0.0;
    double baselineDiffPixelPercent = 0.0;
    int framesWaited = 0;
    int baselineMaxDiffR = 0;
    int baselineMaxDiffG = 0;
    int baselineMaxDiffB = 0;
    bool success = true;
    bool baselineCompared = false;
    bool baselineUpdated = false;
    std::string errorMessage;
};

class VisualTestGameInstance : public NextGameInstanceBase
{
public:
    VisualTestGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~VisualTestGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {}
    void OnSceneLoaded() override;
    bool OnRenderUI() override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;

private:
    bool LoadConfig();
    void PopulateScenesFromSceneList();
    bool ShouldIncludeScene(const std::string& scenePath) const;
    void CaptureAndAdvance();
    void RecordFailureAndAdvance(const std::string& errorMessage);
    void EvaluateBaseline(VisualTestResult& result, const std::string& currentImagePath);
    void GenerateReport();
    void GenerateHtmlReport();
    void GenerateAgentManifest();
    std::string GetRendererName();
    std::string GetSceneName(const std::string& path) const;
    std::string GetSceneCategory(const std::string& path) const;
    std::string GetScreenshotFilename();
    std::string GetBaselineFilename(const VisualTestResult& result) const;

    // Configuration
    std::string outputDir_ = "screenshots/visual_test";
    int defaultFrames_ = 60;
    double loadTimeoutSeconds_ = 20.0;
    std::string baselineDir_ = "assets/visual_test_baselines";
    int diffThreshold_ = 5;
    bool useFastCapture_ = true;
    bool useSceneList_ = true;
    bool updateBaseline_ = false;
    std::vector<std::string> includeExtensions_;
    std::vector<std::string> excludeScenes_;
    std::vector<std::string> excludeSceneContains_;
    std::vector<VisualTestSceneConfig> scenes_;
    std::unordered_map<std::string, double> sceneTimeouts_;

    // State machine
    enum class State { Init, Loading, Rendering, Capturing, Finished };
    State state_ = State::Init;
    size_t currentSceneIndex_ = 0;
    int frameCounter_ = 0;
    bool observedLoadingState_ = false;
    std::chrono::steady_clock::time_point sceneLoadStartTime_;
    std::chrono::steady_clock::time_point sceneStartTime_;

    // Results
    std::vector<VisualTestResult> results_;
};
