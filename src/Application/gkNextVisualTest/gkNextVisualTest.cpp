#include "gkNextVisualTest.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Config/CVarSystem.hpp"
#include "Runtime/ScreenShot.hpp"
#include "Runtime/Subsystems/TaskCoordinator.hpp"
#include "Utilities/FileHelper.hpp"
#include "Vulkan/Device.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fstream>
#include <filesystem>
#include <unordered_set>

using json = nlohmann::json;

namespace
{
    std::string ToLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<VisualTestGameInstance>(config, options, engine);
}

VisualTestGameInstance::VisualTestGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
    , engine_(engine)
{
    config.Title = "gkNextVisualTest";
    // Fast scene triage defaults: enough detail for screenshots, but optimized for throughput.
    options.Width = 1280;
    options.Height = 720;
    options.PresentMode = 0; // Immediate mode for faster testing
    options.ForceSDR = true; // Keep exported review images consistent across HDR/non-HDR desktops.
}

void VisualTestGameInstance::ApplyDefaultCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.samples", "1", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "1", &error);
    cvars.SetDefaultFromString("r.bounces", "2", &error);
    cvars.SetDefaultFromString("r.denoiser", "0", &error);
    cvars.SetDefaultFromString("r.superResolution", "4", &error);
}

void VisualTestGameInstance::OnInit()
{
    SPDLOG_INFO("[VisualTest] Initializing Visual Test Runner...");
    
    if (!LoadConfig())
    {
        SPDLOG_WARN("[VisualTest] Failed to load configuration, using SceneList defaults");
    }

    if (scenes_.empty())
    {
        PopulateScenesFromSceneList();
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
    observedLoadingState_ = false;
    sceneLoadStartTime_ = std::chrono::steady_clock::now();
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
        if (GetEngine().GetEngineStatus() == NextRenderer::EApplicationStatus::Loading)
        {
            observedLoadingState_ = true;
        }

        if (observedLoadingState_ && GetEngine().GetEngineStatus() == NextRenderer::EApplicationStatus::Running)
        {
            RecordFailureAndAdvance("Scene load failed before OnSceneLoaded callback");
            break;
        }

        if (std::chrono::duration<double>(std::chrono::steady_clock::now() - sceneLoadStartTime_).count() >
            loadTimeoutSeconds_)
        {
            RecordFailureAndAdvance(fmt::format("Scene load timed out after {:.1f}s", loadTimeoutSeconds_));
        }
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
        
        GetEngine().SetProgressiveRendering(false, true);
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

        if (config.contains("loadTimeoutSeconds"))
        {
            loadTimeoutSeconds_ = config["loadTimeoutSeconds"].get<double>();
        }

        if (config.contains("useFastCapture"))
        {
            useFastCapture_ = config["useFastCapture"].get<bool>();
        }

        if (config.contains("useSceneList"))
        {
            useSceneList_ = config["useSceneList"].get<bool>();
        }

        if (config.contains("includeExtensions") && config["includeExtensions"].is_array())
        {
            includeExtensions_.clear();
            for (const auto& extJson : config["includeExtensions"])
            {
                includeExtensions_.push_back(ToLowerCopy(extJson.get<std::string>()));
            }
        }

        if (config.contains("excludeScenes") && config["excludeScenes"].is_array())
        {
            excludeScenes_.clear();
            for (const auto& sceneJson : config["excludeScenes"])
            {
                excludeScenes_.push_back(sceneJson.get<std::string>());
            }
        }

        if (config.contains("excludeSceneContains") && config["excludeSceneContains"].is_array())
        {
            excludeSceneContains_.clear();
            for (const auto& itemJson : config["excludeSceneContains"])
            {
                excludeSceneContains_.push_back(itemJson.get<std::string>());
            }
        }

        if (useSceneList_)
        {
            PopulateScenesFromSceneList();
        }
        
        // Parse scenes
        if (config.contains("scenes") && config["scenes"].is_array())
        {
            for (const auto& sceneJson : config["scenes"])
            {
                VisualTestSceneConfig sceneConfig;
                sceneConfig.path = sceneJson["path"].get<std::string>();
                sceneConfig.framesToWait = sceneJson.value("frames", defaultFrames_);
                if (ShouldIncludeScene(sceneConfig.path))
                {
                    scenes_.push_back(sceneConfig);
                }
            }
        }

        std::vector<VisualTestSceneConfig> deduplicatedScenes;
        std::unordered_set<std::string> seenPaths;
        deduplicatedScenes.reserve(scenes_.size());
        for (const auto& scene : scenes_)
        {
            if (seenPaths.insert(scene.path).second)
            {
                deduplicatedScenes.push_back(scene);
            }
        }
        scenes_ = std::move(deduplicatedScenes);
        
        SPDLOG_INFO("[VisualTest] Loaded {} scenes from config", scenes_.size());
        return true;
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("[VisualTest] Failed to parse config file: {}", e.what());
        return false;
    }
}

void VisualTestGameInstance::PopulateScenesFromSceneList()
{
    for (const auto& scene : SceneList::AllScenes)
    {
        if (!ShouldIncludeScene(scene))
        {
            continue;
        }
        scenes_.push_back({scene, defaultFrames_});
    }
}

bool VisualTestGameInstance::ShouldIncludeScene(const std::string& scenePath) const
{
    if (scenePath.empty())
    {
        return false;
    }

    if (std::find(excludeScenes_.begin(), excludeScenes_.end(), scenePath) != excludeScenes_.end())
    {
        return false;
    }

    for (const auto& excluded : excludeSceneContains_)
    {
        if (!excluded.empty() && scenePath.find(excluded) != std::string::npos)
        {
            return false;
        }
    }

    if (!includeExtensions_.empty())
    {
        const std::string extension = ToLowerCopy(std::filesystem::path(scenePath).extension().string());
        if (std::find(includeExtensions_.begin(), includeExtensions_.end(), extension) == includeExtensions_.end())
        {
            return false;
        }
    }

    return true;
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
    if (useFastCapture_)
    {
        ScreenShot::SaveSwapChainToFileFast(&GetEngine().GetRenderer(), screenshotPath, 0, 0, 0, 0);
    }
    else
    {
        ScreenShot::SaveSwapChainToFile(&GetEngine().GetRenderer(), screenshotPath, 0, 0, 0, 0);
    }
    
    // Record result
    VisualTestResult result;
    result.sceneName = GetSceneName(scenes_[currentSceneIndex_].path);
    result.scenePath = scenes_[currentSceneIndex_].path;
    result.sceneCategory = GetSceneCategory(scenes_[currentSceneIndex_].path);
    result.rendererName = GetRendererName();
    result.screenshotPath = screenshotName + ".jpg";
    result.renderTimeSeconds = elapsed;
    result.framesWaited = scenes_[currentSceneIndex_].framesToWait;
    result.success = true;
    results_.push_back(result);
    
    SPDLOG_INFO("[VisualTest] {}/{} - {} captured ({:.2f}s, {} frames)", 
        currentSceneIndex_ + 1, scenes_.size(),
        result.sceneName, elapsed, scenes_[currentSceneIndex_].framesToWait);
    
    // Advance to next scene
    currentSceneIndex_++;
    if (currentSceneIndex_ >= scenes_.size())
    {
        TaskCoordinator::GetInstance()->WaitForAllTasks();
        GenerateReport();
        state_ = State::Finished;
        SPDLOG_INFO("[VisualTest] All tests completed, exiting...");
        GetEngine().RequestClose();
    }
    else
    {
        state_ = State::Loading;
        frameCounter_ = 0;
        observedLoadingState_ = false;
        sceneLoadStartTime_ = std::chrono::steady_clock::now();
        GetEngine().RequestLoadScene(scenes_[currentSceneIndex_].path);
    }
}

void VisualTestGameInstance::RecordFailureAndAdvance(const std::string& errorMessage)
{
    VisualTestResult result;
    result.sceneName = GetSceneName(scenes_[currentSceneIndex_].path);
    result.scenePath = scenes_[currentSceneIndex_].path;
    result.sceneCategory = GetSceneCategory(scenes_[currentSceneIndex_].path);
    result.rendererName = GetRendererName();
    result.success = false;
    result.errorMessage = errorMessage;
    results_.push_back(result);

    SPDLOG_ERROR("[VisualTest] {}/{} - {} failed: {}",
                 currentSceneIndex_ + 1, scenes_.size(), result.sceneName, errorMessage);

    currentSceneIndex_++;
    if (currentSceneIndex_ >= scenes_.size())
    {
        GenerateReport();
        state_ = State::Finished;
        SPDLOG_INFO("[VisualTest] All tests completed, exiting...");
        GetEngine().RequestClose();
        return;
    }

    state_ = State::Loading;
    frameCounter_ = 0;
    observedLoadingState_ = false;
    sceneLoadStartTime_ = std::chrono::steady_clock::now();
    GetEngine().RequestLoadScene(scenes_[currentSceneIndex_].path);
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
    report << fmt::format("**Capture Mode**: {}  \n", useFastCapture_ ? "Fast JPG" : "Standard JPG");
    report << fmt::format("**Default Frames**: {}  \n", defaultFrames_);
    report << fmt::format("**Load Timeout**: {:.1f}s  \n\n", loadTimeoutSeconds_);
    
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
        report << fmt::format("| Path | `{}` |\n", r.scenePath);
        report << fmt::format("| Category | {} |\n", r.sceneCategory);
        report << fmt::format("| Frames | {} |\n", r.framesWaited);
        report << fmt::format("| Render Time | {:.2f}s |\n", r.renderTimeSeconds);
        report << fmt::format("| Status | {} |\n", r.success ? "Passed" : "Failed");
        if (!r.errorMessage.empty())
        {
            report << fmt::format("| Error | {} |\n", r.errorMessage);
        }
        if (r.success)
        {
            report << fmt::format("| Screenshot | ![{}]({}) |\n\n", r.sceneName, r.screenshotPath);
        }
        else
        {
            report << "| Screenshot | N/A |\n\n";
        }
    }
    
    // Write footer
    report << "---\n";
    report << "*Report generated by gkNextRenderer Visual Test Runner for fast scene triage and agent review.*\n";
    
    report.close();
    SPDLOG_INFO("[VisualTest] Report generated: {}", reportPath);

    GenerateAgentManifest();
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

std::string VisualTestGameInstance::GetSceneCategory(const std::string& path) const
{
    const std::string extension = ToLowerCopy(std::filesystem::path(path).extension().string());
    if (extension == ".proc")
    {
        return "Procedural";
    }
    if (extension == ".glb" || extension == ".gltf")
    {
        return "glTF";
    }
    if (extension == ".ldr" || extension == ".mpd")
    {
        return "OMR/LDraw";
    }
    return "Other";
}

std::string VisualTestGameInstance::GetScreenshotFilename()
{
    std::string sceneName = GetSceneName(scenes_[currentSceneIndex_].path);
    return fmt::format("{:03d}_{}_{}", static_cast<int>(currentSceneIndex_), GetRendererName(), sceneName);
}

void VisualTestGameInstance::GenerateAgentManifest()
{
    std::string fullOutputDir = Utilities::FileHelper::GetPlatformFilePath(outputDir_.c_str());
    std::string manifestPath = fullOutputDir + "/agent_review_manifest.json";

    json manifest;
    manifest["version"] = 1;
    manifest["purpose"] = "Fast scene triage screenshots for agent review";
    manifest["renderer"] = GetRendererName();
    manifest["outputDir"] = outputDir_;
    manifest["defaultFramesToWait"] = defaultFrames_;
    manifest["useFastCapture"] = useFastCapture_;
    manifest["loadTimeoutSeconds"] = loadTimeoutSeconds_;
    manifest["results"] = json::array();

    for (const auto& result : results_)
    {
        json item;
        item["sceneName"] = result.sceneName;
        item["scenePath"] = result.scenePath;
        item["sceneCategory"] = result.sceneCategory;
        item["rendererName"] = result.rendererName;
        item["framesWaited"] = result.framesWaited;
        item["renderTimeSeconds"] = result.renderTimeSeconds;
        item["success"] = result.success;
        item["errorMessage"] = result.errorMessage;
        item["screenshotPath"] = result.screenshotPath;
        manifest["results"].push_back(std::move(item));
    }

    std::ofstream manifestFile(manifestPath);
    if (!manifestFile.is_open())
    {
        SPDLOG_ERROR("[VisualTest] Failed to create agent manifest: {}", manifestPath);
        return;
    }

    manifestFile << manifest.dump(2);
    manifestFile.close();
    SPDLOG_INFO("[VisualTest] Agent manifest generated: {}", manifestPath);
}
