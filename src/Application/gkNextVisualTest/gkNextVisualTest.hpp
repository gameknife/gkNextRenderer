#pragma once
#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"

struct VisualTestSceneConfig
{
    std::string path;
    int framesToWait = 120;
};

struct VisualTestResult
{
    std::string sceneName;
    std::string rendererName;
    std::string screenshotPath;
    double renderTimeSeconds = 0.0;
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

    NextEngine& GetEngine() { return *engine_; }
    const NextEngine& GetEngine() const { return *engine_; }

private:
    bool LoadConfig();
    void CaptureAndAdvance();
    void GenerateReport();
    std::string GetRendererName();
    std::string GetSceneName(const std::string& path) const;
    std::string GetScreenshotFilename();

    NextEngine* engine_;

    // Configuration
    std::string outputDir_ = "screenshots/visual_test";
    int defaultFrames_ = 120;
    std::vector<VisualTestSceneConfig> scenes_;

    // State machine
    enum class State { Init, Loading, Rendering, Capturing, Finished };
    State state_ = State::Init;
    size_t currentSceneIndex_ = 0;
    int frameCounter_ = 0;
    std::chrono::steady_clock::time_point sceneStartTime_;

    // Results
    std::vector<VisualTestResult> results_;
};
