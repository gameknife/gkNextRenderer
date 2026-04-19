#pragma once
#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"

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
    double renderTimeSeconds = 0.0;
    int framesWaited = 0;
    bool success = true;
    std::string errorMessage;
};

class VisualTestGameInstance : public NextGameInstanceBase
{
public:
    VisualTestGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~VisualTestGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {}
    void OnSceneLoaded() override;
    bool OnRenderUI() override;
    void ApplyDefaultCVars(NextCVar::FCVarSystem& cvars) override;

    NextEngine& GetEngine() { return *engine_; }
    const NextEngine& GetEngine() const { return *engine_; }

private:
    bool LoadConfig();
    void PopulateScenesFromSceneList();
    bool ShouldIncludeScene(const std::string& scenePath) const;
    void CaptureAndAdvance();
    void RecordFailureAndAdvance(const std::string& errorMessage);
    void GenerateReport();
    void GenerateAgentManifest();
    std::string GetRendererName();
    std::string GetSceneName(const std::string& path) const;
    std::string GetSceneCategory(const std::string& path) const;
    std::string GetScreenshotFilename();

    NextEngine* engine_;

    // Configuration
    std::string outputDir_ = "screenshots/visual_test";
    int defaultFrames_ = 60;
    double loadTimeoutSeconds_ = 20.0;
    bool useFastCapture_ = true;
    bool useSceneList_ = true;
    std::vector<std::string> includeExtensions_;
    std::vector<std::string> excludeScenes_;
    std::vector<std::string> excludeSceneContains_;
    std::vector<VisualTestSceneConfig> scenes_;

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
