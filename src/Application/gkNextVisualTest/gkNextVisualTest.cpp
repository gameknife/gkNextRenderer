#include "gkNextVisualTest.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/ScreenShot.hpp"
#include "Utilities/FileHelper.hpp"
#include "Vulkan/Device.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<VisualTestGameInstance>(config, options, engine);
}

VisualTestGameInstance::VisualTestGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
    , engine_(engine)
{
    config.Title = "gkNextVisualTest";
    // Use reasonable defaults for visual testing
    options.Width = 1280;
    options.Height = 720;
    options.PresentMode = 0; // Immediate mode for faster testing
}

void VisualTestGameInstance::OnInit()
{
    SPDLOG_INFO("[VisualTest] Initializing Visual Test Runner...");
    
    if (!LoadConfig())
    {
        SPDLOG_ERROR("[VisualTest] Failed to load configuration, using default scenes");
        // Fallback to all scenes from SceneList
        for (const auto& scene : SceneList::AllScenes)
        {
            scenes_.push_back({scene, defaultFrames_});
        }
    }
    
    if (scenes_.empty())
    {
        SPDLOG_ERROR("[VisualTest] No scenes to test, exiting");
        GetEngine().RequestClose();
        return;
    }
    
    // Ensure output directory exists
    std::string fullOutputDir = Utilities::FileHelper::GetPlatformFilePath(outputDir_.c_str());
    Utilities::FileHelper::EnsureDirectoryExists(fullOutputDir);
    
    SPDLOG_INFO("[VisualTest] Starting visual test with {} scenes", scenes_.size());
    SPDLOG_INFO("[VisualTest] Output directory: {}", fullOutputDir);
    
    // Load first scene
    state_ = State::Loading;
    GetEngine().RequestLoadScene(scenes_[0].path);
}

void VisualTestGameInstance::OnTick(double deltaSeconds)
{
    switch (state_)
    {
    case State::Init:
        // Should not happen, OnInit handles this
        break;
        
    case State::Loading:
        // Waiting for OnSceneLoaded callback
        break;
        
    case State::Rendering:
        frameCounter_++;
        if (frameCounter_ >= scenes_[currentSceneIndex_].framesToWait)
        {
            state_ = State::Capturing;
        }
        break;
        
    case State::Capturing:
        CaptureAndAdvance();
        break;
        
    case State::Finished:
        // Already handled
        break;
    }
}

void VisualTestGameInstance::OnSceneLoaded()
{
    if (state_ == State::Loading)
    {
        SPDLOG_INFO("[VisualTest] Scene {}/{} loaded: {}", 
            currentSceneIndex_ + 1, scenes_.size(),
            GetSceneName(scenes_[currentSceneIndex_].path));
        
        frameCounter_ = 0;
        sceneStartTime_ = std::chrono::steady_clock::now();
        state_ = State::Rendering;
    }
}

bool VisualTestGameInstance::OnRenderUI()
{
    // Hide UI during visual test to get clean screenshots
    return true;
}

bool VisualTestGameInstance::LoadConfig()
{
    // Search paths for configuration file
    std::vector<std::string> searchPaths = {
        "configs/visual_test.json",
        "assets/configs/visual_test.json"
    };
    
    std::string configPath;
    for (const auto& path : searchPaths)
    {
        std::string fullPath = Utilities::FileHelper::GetPlatformFilePath(path.c_str());
        if (std::filesystem::exists(fullPath))
        {
            configPath = fullPath;
            break;
        }
    }
    
    if (configPath.empty())
    {
        SPDLOG_WARN("[VisualTest] No visual_test.json found, will use SceneList::AllScenes");
        return false;
    }
    
    SPDLOG_INFO("[VisualTest] Loading configuration from: {}", configPath);
    
    try
    {
        std::ifstream file(configPath);
        if (!file.is_open())
        {
            SPDLOG_ERROR("[VisualTest] Failed to open config file: {}", configPath);
            return false;
        }
        
        json config = json::parse(file);
        
        // Parse output directory
        if (config.contains("outputDir"))
        {
            outputDir_ = config["outputDir"].get<std::string>();
        }
        
        // Parse default frames
        if (config.contains("defaultFramesToWait"))
        {
            defaultFrames_ = config["defaultFramesToWait"].get<int>();
        }
        
        // Parse scenes
        if (config.contains("scenes") && config["scenes"].is_array())
        {
            for (const auto& sceneJson : config["scenes"])
            {
                VisualTestSceneConfig sceneConfig;
                sceneConfig.path = sceneJson["path"].get<std::string>();
                sceneConfig.framesToWait = sceneJson.value("frames", defaultFrames_);
                scenes_.push_back(sceneConfig);
            }
        }
        
        SPDLOG_INFO("[VisualTest] Loaded {} scenes from config", scenes_.size());
        return !scenes_.empty();
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("[VisualTest] Failed to parse config file: {}", e.what());
        return false;
    }
}

void VisualTestGameInstance::CaptureAndAdvance()
{
    // Calculate render time
    auto endTime = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(endTime - sceneStartTime_).count();
    
    // Generate screenshot filename
    std::string screenshotName = GetScreenshotFilename();
    std::string fullOutputDir = Utilities::FileHelper::GetPlatformFilePath(outputDir_.c_str());
    std::string screenshotPath = fullOutputDir + "/" + screenshotName;
    
    // Take screenshot
    ScreenShot::SaveSwapChainToFile(&GetEngine().GetRenderer(), screenshotPath, 0, 0, 0, 0);
    
    // Record result
    VisualTestResult result;
    result.sceneName = GetSceneName(scenes_[currentSceneIndex_].path);
    result.rendererName = GetRendererName();
    result.screenshotPath = screenshotName + ".jpg";
    result.renderTimeSeconds = elapsed;
    result.success = true;
    results_.push_back(result);
    
    SPDLOG_INFO("[VisualTest] {}/{} - {} captured ({:.2f}s, {} frames)", 
        currentSceneIndex_ + 1, scenes_.size(),
        result.sceneName, elapsed, scenes_[currentSceneIndex_].framesToWait);
    
    // Advance to next scene
    currentSceneIndex_++;
    if (currentSceneIndex_ >= scenes_.size())
    {
        GenerateReport();
        state_ = State::Finished;
        SPDLOG_INFO("[VisualTest] All tests completed, exiting...");
        GetEngine().RequestClose();
    }
    else
    {
        state_ = State::Loading;
        frameCounter_ = 0;
        GetEngine().RequestLoadScene(scenes_[currentSceneIndex_].path);
    }
}

void VisualTestGameInstance::GenerateReport()
{
    std::string fullOutputDir = Utilities::FileHelper::GetPlatformFilePath(outputDir_.c_str());
    std::string reportPath = fullOutputDir + "/visual_test_report.md";
    
    std::ofstream report(reportPath);
    if (!report.is_open())
    {
        SPDLOG_ERROR("[VisualTest] Failed to create report file: {}", reportPath);
        return;
    }
    
    // Get system info
    VkPhysicalDeviceProperties deviceProp{};
    vkGetPhysicalDeviceProperties(GetEngine().GetRenderer().Device().PhysicalDevice(), &deviceProp);
    
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&t);
    
    // Count results
    int passed = 0, failed = 0;
    for (const auto& r : results_)
    {
        if (r.success) passed++;
        else failed++;
    }
    
    // Write header
    report << "# Visual Test Report\n\n";
    report << fmt::format("**Generated**: {:%Y-%m-%d %H:%M:%S}  \n", *tm);
#if defined(_WIN32)
    report << "**Platform**: Windows x86_64  \n";
#elif defined(__APPLE__)
    report << "**Platform**: macOS  \n";
#else
    report << "**Platform**: Linux x86_64  \n";
#endif
    report << fmt::format("**GPU**: {}  \n", deviceProp.deviceName);
    report << fmt::format("**Renderer**: {}  \n\n", GetRendererName());
    
    // Write summary
    report << "## Summary\n\n";
    report << "| Status | Count |\n";
    report << "|--------|-------|\n";
    report << fmt::format("| Passed | {} |\n", passed);
    report << fmt::format("| Failed | {} |\n", failed);
    report << fmt::format("| **Total** | **{}** |\n\n", results_.size());
    
    // Write individual results
    report << "## Test Results\n\n";
    for (size_t i = 0; i < results_.size(); ++i)
    {
        const auto& r = results_[i];
        report << fmt::format("### {}. {}\n\n", i + 1, r.sceneName);
        report << "| Property | Value |\n";
        report << "|----------|-------|\n";
        report << fmt::format("| Path | `{}` |\n", scenes_[i].path);
        report << fmt::format("| Frames | {} |\n", scenes_[i].framesToWait);
        report << fmt::format("| Render Time | {:.2f}s |\n", r.renderTimeSeconds);
        report << fmt::format("| Status | {} |\n", r.success ? "Passed" : "Failed");
        report << fmt::format("| Screenshot | ![{}]({}) |\n\n", r.sceneName, r.screenshotPath);
    }
    
    // Write footer
    report << "---\n";
    report << "*Report generated by gkNextRenderer Visual Test Runner*\n";
    
    report.close();
    SPDLOG_INFO("[VisualTest] Report generated: {}", reportPath);
}

std::string VisualTestGameInstance::GetRendererName()
{
    auto rendererType = GetEngine().GetRenderer().CurrentLogicRendererType();
    switch (rendererType)
    {
    case Vulkan::ERT_PathTracing:    return "PathTracing";
    case Vulkan::ERT_ModernDeferred: return "SoftTracing";
    case Vulkan::ERT_LegacyDeferred: return "SoftModern";
    case Vulkan::ERT_VoxelTracing:   return "VoxelTracing";
    default:                         return "Unknown";
    }
}

std::string VisualTestGameInstance::GetSceneName(const std::string& path) const
{
    std::filesystem::path p(path);
    return p.stem().string();
}

std::string VisualTestGameInstance::GetScreenshotFilename()
{
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm = std::localtime(&t);
    
    std::string sceneName = GetSceneName(scenes_[currentSceneIndex_].path);
    std::string rendererName = GetRendererName();
    std::string timestamp = fmt::format("{:%Y-%m-%d_%H-%M-%S}", *tm);
    
    return fmt::format("{}_{}_{}", sceneName, rendererName, timestamp);
}
