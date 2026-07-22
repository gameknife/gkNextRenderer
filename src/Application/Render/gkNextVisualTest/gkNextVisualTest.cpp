#include "gkNextVisualTest.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Utilities/StbImage.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/Device.hpp"

#include <stb_image_write.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <unordered_set>
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Application/Common/DemoScenes.hpp"

using json = nlohmann::json;

namespace
{
    std::string ToLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string FormatBaselineStatus(const VisualTestResult& result)
    {
        if (!result.success)
        {
            return "N/A";
        }
        if (result.baselineUpdated)
        {
            return "Updated";
        }
        if (result.baselineCompared)
        {
            return "Compared";
        }
        if (!result.baselineStatus.empty())
        {
            return result.baselineStatus;
        }
        return "No baseline";
    }

    std::string HtmlEscape(const std::string& value)
    {
        std::string escaped;
        escaped.reserve(value.size());
        for (char c : value)
        {
            switch (c)
            {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default: escaped += c; break;
            }
        }
        return escaped;
    }

    std::string MakeReportRelativePath(const std::string& targetPath, const std::string& reportDir)
    {
        std::error_code error;
        std::filesystem::path relativePath = std::filesystem::relative(targetPath, reportDir, error);
        if (error)
        {
            return std::filesystem::path(targetPath).generic_string();
        }
        return relativePath.generic_string();
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    Modules::LDraw::Register();
    Modules::Scad::Register();
    AppCommon::RegisterDemoScenes();
    return std::make_unique<VisualTestGameInstance>(config, options, engine);
}

VisualTestGameInstance::VisualTestGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    config.Title = "gkNextVisualTest";
    // Fast scene triage defaults: enough detail for screenshots, but optimized for throughput.
    options.Width = 1280;
    options.Height = 720;
    options.PresentMode = 0; // Immediate mode for faster testing
    options.ForceSDR = true; // Keep exported review images consistent across HDR/non-HDR desktops.
    updateBaseline_ = options.UpdateVisualTestBaseline;
}

void VisualTestGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.samples", "1", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "1", &error);
    cvars.SetDefaultFromString("r.bounces", "2", &error);
    cvars.SetDefaultFromString("r.upscaler.qualityMode", "4", &error);
}

void VisualTestGameInstance::OnInit()
{
    SPDLOG_INFO("[VisualTest] Initializing Visual Test Runner...");
    
    if (!LoadConfig())
    {
        SPDLOG_WARN("[VisualTest] Failed to load configuration, using Runtime::Scene::SceneList defaults");
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
    SPDLOG_INFO("[VisualTest] Baseline directory: {}{}", Utilities::FileHelper::GetPlatformFilePath(baselineDir_.c_str()),
                updateBaseline_ ? " (update enabled)" : "");
    
    // Load first scene
    state_ = State::Loading;
    observedLoadingState_ = false;
    sceneLoadStartTime_ = std::chrono::steady_clock::now();
    GetEngine().RequestLoadScene({.filename = scenes_[0].path});
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

        {
            double effectiveTimeout = loadTimeoutSeconds_;
            if (currentSceneIndex_ < scenes_.size())
            {
                auto it = sceneTimeouts_.find(scenes_[currentSceneIndex_].path);
                if (it != sceneTimeouts_.end())
                {
                    effectiveTimeout = it->second;
                }
            }
            if (std::chrono::duration<double>(std::chrono::steady_clock::now() - sceneLoadStartTime_).count() >
                effectiveTimeout)
            {
                RecordFailureAndAdvance(fmt::format("Scene load timed out after {:.1f}s", effectiveTimeout));
            }
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
        SPDLOG_WARN("[VisualTest] No visual_test.json found, will use Runtime::Scene::SceneList::AllScenes");
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

        if (config.contains("baselineDir"))
        {
            baselineDir_ = config["baselineDir"].get<std::string>();
        }

        if (config.contains("diffThreshold"))
        {
            diffThreshold_ = config["diffThreshold"].get<int>();
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
                if (sceneJson.is_string())
                {
                    sceneConfig.path = sceneJson.get<std::string>();
                    sceneConfig.framesToWait = defaultFrames_;
                }
                else
                {
                    sceneConfig.path = sceneJson["path"].get<std::string>();
                    sceneConfig.framesToWait = sceneJson.value("frames", defaultFrames_);
                }
                if (ShouldIncludeScene(sceneConfig.path))
                {
                    scenes_.push_back(sceneConfig);
                }
            }
        }

        // Parse per-scene timeout overrides
        if (config.contains("sceneTimeouts") && config["sceneTimeouts"].is_object())
        {
            for (const auto& [key, value] : config["sceneTimeouts"].items())
            {
                if (value.is_number())
                {
                    sceneTimeouts_[key] = value.get<double>();
                }
                else
                {
                    SPDLOG_WARN("[VisualTest] Invalid timeout value for scene '{}', skipping", key);
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
    for (const auto& scene : Runtime::Scene::SceneList::AllScenes)
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
    // Generate screenshot filename
    std::string screenshotName = GetScreenshotFilename();
    std::string fullOutputDir = Utilities::FileHelper::GetPlatformFilePath(outputDir_.c_str());
    std::string screenshotPath = fullOutputDir + "/" + screenshotName;

    if (!captureRequested_)
    {
        captureRenderTimeSeconds_ = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - sceneStartTime_).count();
        captureRequested_ = true;
        GetEngine().RequestScreenShot({.filename = screenshotPath});
        return;
    }

    if (GetEngine().IsCapturingScreenShot())
    {
        return;
    }

    Tasks::TaskCoordinator::GetInstance()->WaitForAllTasks();
    
    // Record result
    VisualTestResult result;
    result.sceneName = GetSceneName(scenes_[currentSceneIndex_].path);
    result.scenePath = scenes_[currentSceneIndex_].path;
    result.sceneCategory = GetSceneCategory(scenes_[currentSceneIndex_].path);
    result.rendererName = GetRendererName();
    result.screenshotPath = screenshotName + ".jpg";
    result.renderTimeSeconds = captureRenderTimeSeconds_;
    result.framesWaited = scenes_[currentSceneIndex_].framesToWait;
    result.success = true;
    EvaluateBaseline(result, screenshotPath + ".jpg");
    results_.push_back(result);
    captureRequested_ = false;
    
    SPDLOG_INFO("[VisualTest] {}/{} - {} captured ({:.2f}s, {} frames)", 
        currentSceneIndex_ + 1, scenes_.size(),
        result.sceneName, captureRenderTimeSeconds_, scenes_[currentSceneIndex_].framesToWait);
    
    // Advance to next scene
    currentSceneIndex_++;
    if (currentSceneIndex_ >= scenes_.size())
    {
        Tasks::TaskCoordinator::GetInstance()->WaitForAllTasks();
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
        GetEngine().RequestLoadScene({.filename = scenes_[currentSceneIndex_].path});
    }
}

void VisualTestGameInstance::EvaluateBaseline(VisualTestResult& result, const std::string& currentImagePath)
{
    const std::string baselineDir = Utilities::FileHelper::GetPlatformFilePath(baselineDir_.c_str());
    const std::string baselineFilename = GetBaselineFilename(result);
    const std::string baselinePath = baselineDir + "/" + baselineFilename;
    result.baselinePath = baselineFilename;

    int currentWidth = 0;
    int currentHeight = 0;
    int currentChannels = 0;
    unsigned char* currentPixels = stbi_load(currentImagePath.c_str(), &currentWidth, &currentHeight, &currentChannels, 4);
    if (currentPixels == nullptr)
    {
        result.baselineStatus = "current image load failed";
        SPDLOG_WARN("[VisualTest] Failed to load current image for baseline comparison: {}", currentImagePath);
        return;
    }

    if (updateBaseline_)
    {
        Utilities::FileHelper::EnsureDirectoryExists(baselineDir);
        if (stbi_write_png(baselinePath.c_str(), currentWidth, currentHeight, 4, currentPixels, currentWidth * 4) == 0)
        {
            result.baselineStatus = "baseline update failed";
            SPDLOG_WARN("[VisualTest] Failed to update baseline {}", baselinePath);
            stbi_image_free(currentPixels);
            return;
        }
        result.baselineUpdated = true;
        result.baselineStatus = "updated";
    }

    if (!std::filesystem::exists(baselinePath))
    {
        result.baselineStatus = "no baseline";
        SPDLOG_WARN("[VisualTest] Missing baseline for {}: {}", result.sceneName, baselinePath);
        stbi_image_free(currentPixels);
        return;
    }

    int baselineWidth = 0;
    int baselineHeight = 0;
    int baselineChannels = 0;
    unsigned char* baselinePixels = stbi_load(baselinePath.c_str(), &baselineWidth, &baselineHeight, &baselineChannels, 4);

    if (baselinePixels == nullptr)
    {
        result.baselineStatus = "baseline image load failed";
        SPDLOG_WARN("[VisualTest] Failed to load baseline image for {}: {}", result.sceneName, baselinePath);
        stbi_image_free(currentPixels);
        return;
    }

    if (currentWidth != baselineWidth || currentHeight != baselineHeight)
    {
        result.baselineStatus = fmt::format("size mismatch (current {}x{}, baseline {}x{})",
                                            currentWidth, currentHeight, baselineWidth, baselineHeight);
        stbi_image_free(currentPixels);
        stbi_image_free(baselinePixels);
        return;
    }

    double squaredErrorSum = 0.0;
    uint64_t diffPixelCount = 0;
    const uint64_t pixelCount = static_cast<uint64_t>(currentWidth) * static_cast<uint64_t>(currentHeight);
    std::vector<unsigned char> diffPixels;
    diffPixels.resize(pixelCount * 4);
    for (uint64_t pixel = 0; pixel < pixelCount; ++pixel)
    {
        const uint64_t offset = pixel * 4;
        int pixelMaxDiff = 0;
        for (int channel = 0; channel < 3; ++channel)
        {
            const int diff = std::abs(static_cast<int>(currentPixels[offset + channel]) -
                                      static_cast<int>(baselinePixels[offset + channel]));
            squaredErrorSum += static_cast<double>(diff * diff);
            pixelMaxDiff = std::max(pixelMaxDiff, diff);
            if (channel == 0)
            {
                result.baselineMaxDiffR = std::max(result.baselineMaxDiffR, diff);
            }
            else if (channel == 1)
            {
                result.baselineMaxDiffG = std::max(result.baselineMaxDiffG, diff);
            }
            else
            {
                result.baselineMaxDiffB = std::max(result.baselineMaxDiffB, diff);
            }
            diffPixels[offset + channel] = static_cast<unsigned char>(std::min(diff * 4, 255));
        }
        diffPixels[offset + 3] = 255;
        if (pixelMaxDiff > diffThreshold_)
        {
            diffPixelCount++;
        }
    }

    result.baselineRmse = std::sqrt(squaredErrorSum / static_cast<double>(pixelCount * 3));
    result.baselineDiffPixelPercent = pixelCount > 0 ? (static_cast<double>(diffPixelCount) * 100.0 / static_cast<double>(pixelCount)) : 0.0;
    result.baselineCompared = true;
    if (result.baselineStatus.empty())
    {
        result.baselineStatus = "compared";
    }

    const std::string fullOutputDir = Utilities::FileHelper::GetPlatformFilePath(outputDir_.c_str());
    const std::string diffStem = std::filesystem::path(result.screenshotPath).stem().string();
    result.diffImagePath = diffStem + "_diff.png";
    const std::string diffPath = fullOutputDir + "/" + result.diffImagePath;
    if (stbi_write_png(diffPath.c_str(), currentWidth, currentHeight, 4, diffPixels.data(), currentWidth * 4) == 0)
    {
        SPDLOG_WARN("[VisualTest] Failed to write diff image: {}", diffPath);
        result.diffImagePath.clear();
    }

    stbi_image_free(currentPixels);
    stbi_image_free(baselinePixels);
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
    GetEngine().RequestLoadScene({.filename = scenes_[currentSceneIndex_].path});
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
    report << "**Capture Mode**: Asynchronous screenshot export  \n";
    report << fmt::format("**Default Frames**: {}  \n", defaultFrames_);
    report << fmt::format("**Baseline Directory**: `{}`  \n", baselineDir_);
    report << fmt::format("**Diff Threshold**: {}  \n", diffThreshold_);
    report << fmt::format("**Update Baseline**: {}  \n", updateBaseline_ ? "true" : "false");
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
        report << fmt::format("| Baseline | {} |\n", FormatBaselineStatus(r));
        if (r.success && !r.baselinePath.empty())
        {
            report << fmt::format("| Baseline Path | `{}` |\n", r.baselinePath);
        }
        if (r.baselineCompared)
        {
            report << fmt::format("| RMSE | {:.3f} |\n", r.baselineRmse);
            report << fmt::format("| Max RGB Diff | R{} G{} B{} |\n",
                                  r.baselineMaxDiffR, r.baselineMaxDiffG, r.baselineMaxDiffB);
            report << fmt::format("| Diff Pixels > Threshold | {:.4f}% |\n", r.baselineDiffPixelPercent);
        }
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

    GenerateHtmlReport();
    GenerateAgentManifest();
}

void VisualTestGameInstance::GenerateHtmlReport()
{
    const std::string fullOutputDir = Utilities::FileHelper::GetPlatformFilePath(outputDir_.c_str());
    const std::string reportPath = fullOutputDir + "/visual_test_report.html";
    const std::string fullBaselineDir = Utilities::FileHelper::GetPlatformFilePath(baselineDir_.c_str());

    std::ofstream report(reportPath);
    if (!report.is_open())
    {
        SPDLOG_ERROR("[VisualTest] Failed to create HTML report file: {}", reportPath);
        return;
    }

    report << "<!doctype html><html><head><meta charset=\"utf-8\">";
    report << "<title>gkNext Visual Test Report</title>";
    report << "<style>body{font-family:Segoe UI,Arial,sans-serif;margin:24px;color:#1f2328;background:#f6f8fa}"
              "table{border-collapse:collapse;width:100%;background:#fff}th,td{border:1px solid #d0d7de;padding:8px;vertical-align:top}"
              "th{background:#eaeef2;text-align:left}img{max-width:320px;height:auto;border:1px solid #d0d7de;background:#fff}"
              ".meta{margin-bottom:16px}.muted{color:#656d76}.num{white-space:nowrap}</style>";
    report << "</head><body>";
    report << "<h1>Visual Test Report</h1>";
    report << "<div class=\"meta\">";
    report << "<div><b>Renderer:</b> " << HtmlEscape(GetRendererName()) << "</div>";
    report << "<div><b>Capture Mode:</b> Asynchronous screenshot export</div>";
    report << "<div><b>Baseline Directory:</b> <code>" << HtmlEscape(baselineDir_) << "</code></div>";
    report << "<div><b>Diff Threshold:</b> " << diffThreshold_ << "</div>";
    report << "<div><b>Update Baseline:</b> " << (updateBaseline_ ? "true" : "false") << "</div>";
    report << "</div>";
    report << "<table><thead><tr><th>Scene</th><th>Baseline</th><th>Current</th><th>Diff</th><th>Stats</th></tr></thead><tbody>";

    for (const auto& result : results_)
    {
        const std::string baselineImagePath = !result.baselinePath.empty()
            ? MakeReportRelativePath(fullBaselineDir + "/" + result.baselinePath, fullOutputDir)
            : std::string();

        report << "<tr>";
        report << "<td><b>" << HtmlEscape(result.sceneName) << "</b><br><span class=\"muted\">"
               << HtmlEscape(result.scenePath) << "</span></td>";
        report << "<td>";
        if (result.baselineCompared || result.baselineUpdated)
        {
            report << "<img src=\"" << HtmlEscape(baselineImagePath) << "\" alt=\"baseline\">";
        }
        else
        {
            report << "<span class=\"muted\">" << HtmlEscape(FormatBaselineStatus(result)) << "</span>";
        }
        report << "</td>";
        report << "<td>";
        if (result.success)
        {
            report << "<img src=\"" << HtmlEscape(result.screenshotPath) << "\" alt=\"current\">";
        }
        else
        {
            report << "<span class=\"muted\">Failed</span>";
        }
        report << "</td>";
        report << "<td>";
        if (!result.diffImagePath.empty())
        {
            report << "<img src=\"" << HtmlEscape(result.diffImagePath) << "\" alt=\"diff\">";
        }
        else
        {
            report << "<span class=\"muted\">N/A</span>";
        }
        report << "</td>";
        report << "<td class=\"num\">";
        report << "Status: " << HtmlEscape(FormatBaselineStatus(result)) << "<br>";
        if (result.baselineCompared)
        {
            report << fmt::format("RMSE: {:.3f}<br>", result.baselineRmse);
            report << fmt::format("Max RGB: R{} G{} B{}<br>",
                                  result.baselineMaxDiffR, result.baselineMaxDiffG, result.baselineMaxDiffB);
            report << fmt::format("Pixels &gt; threshold: {:.4f}%", result.baselineDiffPixelPercent);
        }
        if (!result.errorMessage.empty())
        {
            report << "<br>Error: " << HtmlEscape(result.errorMessage);
        }
        report << "</td>";
        report << "</tr>";
    }

    report << "</tbody></table></body></html>";
    report.close();
    SPDLOG_INFO("[VisualTest] HTML report generated: {}", reportPath);
}

std::string VisualTestGameInstance::GetRendererName()
{
    return Vulkan::GetRendererName(GetEngine().GetRenderer().CurrentLogicRendererType());
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

std::string VisualTestGameInstance::GetBaselineFilename(const VisualTestResult& result) const
{
    return fmt::format("{}.png", result.sceneName);
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
    manifest["baselineDir"] = baselineDir_;
    manifest["diffThreshold"] = diffThreshold_;
    manifest["updateBaseline"] = updateBaseline_;
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
        item["baselinePath"] = result.baselinePath;
        item["diffImagePath"] = result.diffImagePath;
        item["baselineStatus"] = result.baselineStatus;
        item["baselineCompared"] = result.baselineCompared;
        item["baselineUpdated"] = result.baselineUpdated;
        item["baselineRmse"] = result.baselineRmse;
        item["baselineMaxDiffR"] = result.baselineMaxDiffR;
        item["baselineMaxDiffG"] = result.baselineMaxDiffG;
        item["baselineMaxDiffB"] = result.baselineMaxDiffB;
        item["baselineDiffPixelPercent"] = result.baselineDiffPixelPercent;
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
