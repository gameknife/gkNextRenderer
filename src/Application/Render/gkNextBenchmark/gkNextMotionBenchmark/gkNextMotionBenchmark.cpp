#include "gkNextMotionBenchmark.hpp"
#include "Common/BenchMark.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Application/Common/DemoScenes.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>

using json = nlohmann::json;

namespace
{
    std::string Trim(std::string value)
    {
        const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch)
        {
            return std::isspace(ch) != 0;
        });
        const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch)
        {
            return std::isspace(ch) != 0;
        }).base();
        if (begin >= end)
        {
            return {};
        }
        return std::string(begin, end);
    }

    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch)
        {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    std::optional<int32_t> ParseRendererType(std::string value)
    {
        value = ToLower(Trim(value));
        if (value.empty())
        {
            return std::nullopt;
        }
        if (std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; }))
        {
            return std::stoi(value);
        }
        for (const Vulkan::ERendererType type : {Vulkan::ERT_PathTracing,
                                                 Vulkan::ERT_SoftwareTracing,
                                                 Vulkan::ERT_SoftwareModern,
                                                 Vulkan::ERT_VoxelTracing,
                                                 Vulkan::ERT_SoftwareModernNoAmbient})
        {
            if (value == ToLower(Vulkan::GetRendererName(type)))
            {
                return type;
            }
        }
        return std::nullopt;
    }

    std::string RendererNameForValue(const std::string& value)
    {
        if (const std::optional<int32_t> type = ParseRendererType(value))
        {
            return Vulkan::GetRendererName(static_cast<Vulkan::ERendererType>(*type));
        }
        return value;
    }

    std::filesystem::path ResolveConfigPath(const std::string& configPath)
    {
        if (configPath.empty())
        {
            return {};
        }
        std::filesystem::path path(configPath);
        if (path.is_absolute() && std::filesystem::exists(path))
        {
            return path;
        }
        if (std::filesystem::exists(path))
        {
            return std::filesystem::absolute(path);
        }
        return Utilities::FileHelper::GetPlatformFilePath(configPath.c_str());
    }

    std::string CVarValueFromJson(const json& value)
    {
        if (value.is_boolean())
        {
            return value.get<bool>() ? "1" : "0";
        }
        if (value.is_number_integer())
        {
            return std::to_string(value.get<int64_t>());
        }
        if (value.is_number_unsigned())
        {
            return std::to_string(value.get<uint64_t>());
        }
        if (value.is_number_float())
        {
            return fmt::format("{}", value.get<double>());
        }
        if (value.is_string())
        {
            return value.get<std::string>();
        }
        return value.dump();
    }

    std::map<std::string, std::string> ParseCVarMap(const json& cvars)
    {
        std::map<std::string, std::string> result;
        if (!cvars.is_object())
        {
            return result;
        }
        for (const auto& [key, value] : cvars.items())
        {
            result[key] = CVarValueFromJson(value);
        }
        return result;
    }

    void MergeCVarMap(std::map<std::string, std::string>& target, const std::map<std::string, std::string>& source)
    {
        for (const auto& [key, value] : source)
        {
            target[key] = value;
        }
    }

    std::string ScenePathFromJson(const json& scene)
    {
        if (scene.is_string())
        {
            return scene.get<std::string>();
        }
        if (scene.is_object())
        {
            return scene.value("path", scene.value("scene", ""));
        }
        return {};
    }

    std::string SceneLabelFromJson(const json& scene, const std::string& scenePath)
    {
        if (scene.is_object() && scene.contains("name"))
        {
            return scene["name"].get<std::string>();
        }
        return std::filesystem::path(scenePath).filename().replace_extension().string();
    }
}

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options,
                                                         NextEngine* engine)
{
    Modules::LDraw::Register();
    Modules::Scad::Register();
    AppCommon::RegisterDemoScenes();
    return std::make_unique<BenchmarkGameInstance>(config, options, engine);
}

BenchmarkGameInstance::BenchmarkGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    config.Title = "gkNextMotionBenchmark";
    options.PresentMode = 0;
    LoadConfig(options, config);
}

void BenchmarkGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    for (const auto& [name, value] : defaultCvars_)
    {
        if (!cvars.SetDefaultFromString(name, value, &error))
        {
            SPDLOG_WARN("[Benchmark] Failed to set default CVar {}={}: {}", name, value, error);
        }
    }
}

void BenchmarkGameInstance::OnInit()
{
    benchMarker_ = std::make_unique<BenchMarker>(benchmarkSettings_);

    if (benchmarkRuns_.empty())
    {
        BuildDefaultRuns(GetEngine().GetOptions());
    }
    if (benchmarkRuns_.empty())
    {
        SPDLOG_ERROR("[Benchmark] No benchmark runs available");
        GetEngine().RequestClose();
        return;
    }

    LoadCurrentRun();
}

void BenchmarkGameInstance::OnTick(double deltaSeconds)
{
    if (benchMarker_ && benchMarker_->OnTick(GetEngine().GetWindow().GetTime(), &(GetEngine().GetRenderer())))
    {
        // Benchmark is done, report the results.
        benchMarker_->OnReport(&(GetEngine().GetRenderer()), benchmarkRuns_[currentRunIndex_].label);
        GetEngine().AddTickedTask([this](double) {
            if (GetEngine().IsCapturingScreenShot())
            {
                return false;
            }
            if (!AdvanceRun())
            {
                GetEngine().RequestClose();
            }
            return true;
        });
    }
    totalTime_ += deltaSeconds * 20.0;
    modelViewController_.SetModelRotation( totalTime_, 0 );
}

void BenchmarkGameInstance::OnSceneLoaded()
{
    benchMarker_->OnSceneStart(GetEngine().GetWindow().GetTime());
    GetEngine().GetScene().PlayAllTracks();
    modelViewController_.Reset(GetEngine().GetScene().GetRenderCamera());
    totalTime_ = 0;
}

bool BenchmarkGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    outRenderCamera.ModelView = modelViewController_.ModelView();
    outRenderCamera.FieldOfView = modelViewController_.FieldOfView();
    return true;
}

bool BenchmarkGameInstance::OnRenderUI()
{
    return true;
}

void BenchmarkGameInstance::LoadConfig(Runtime::Config::Options& options, Vulkan::WindowConfig& config)
{
    benchmarkSettings_ = FBenchmarkSettings{};
    defaultCvars_ = {
        {"r.samples", "4"},
        {"r.temporalFrames", "16"},
        {"r.bounces", "4"},
        {"r.denoiser", "0"},
        {"r.superResolution", "4"},
        {"sys.tickAnimation", "0"},
    };

    const std::filesystem::path configPath = ResolveConfigPath(options.BenchmarkConfig);
    if (configPath.empty())
    {
        return;
    }

    std::ifstream file(configPath);
    if (!file.is_open())
    {
        SPDLOG_ERROR("[Benchmark] Failed to open config: {}", configPath.string());
        return;
    }

    json root;
    try
    {
        root = json::parse(file);
    }
    catch (const std::exception& e)
    {
        SPDLOG_ERROR("[Benchmark] Failed to parse config {}: {}", configPath.string(), e.what());
        return;
    }

    SPDLOG_INFO("[Benchmark] Loading config: {}", configPath.string());

    benchmarkSettings_.warmupSeconds = std::max(0.0, root.value("warmupSeconds", benchmarkSettings_.warmupSeconds));
    benchmarkSettings_.durationSeconds = std::max(0.001, root.value("durationSeconds", benchmarkSettings_.durationSeconds));
    benchmarkSettings_.outputPath = root.value("output", root.value("report", root.value("outputCsv", benchmarkSettings_.outputPath)));

    if (root.contains("resolution") && root["resolution"].is_object())
    {
        options.Width = root["resolution"].value("width", options.Width);
        options.Height = root["resolution"].value("height", options.Height);
        config.Width = options.Width;
        config.Height = options.Height;
    }
    options.Width = root.value("width", options.Width);
    options.Height = root.value("height", options.Height);
    options.PresentMode = root.value("presentMode", options.PresentMode);
    config.Width = options.Width;
    config.Height = options.Height;

    if (root.contains("cvars"))
    {
        MergeCVarMap(defaultCvars_, ParseCVarMap(root["cvars"]));
    }

    auto makeRun = [this](std::string scenePath, std::string sceneLabel, std::string renderer,
                          std::map<std::string, std::string> cvars)
    {
        scenePath = Trim(std::move(scenePath));
        if (scenePath.empty())
        {
            return;
        }

        FBenchmarkRun run{};
        run.scene = scenePath;
        run.rendererName = RendererNameForValue(renderer);
        run.label = sceneLabel.empty() ? std::filesystem::path(scenePath).filename().replace_extension().string() : sceneLabel;
        run.cvars = std::move(cvars);
        if (const std::optional<int32_t> rendererType = ParseRendererType(renderer))
        {
            run.cvars["r.rendererType"] = std::to_string(*rendererType);
        }
        benchmarkRuns_.push_back(std::move(run));
    };

    if (root.contains("runs") && root["runs"].is_array())
    {
        for (const json& runJson : root["runs"])
        {
            if (!runJson.is_object())
            {
                continue;
            }

            std::map<std::string, std::string> cvars = defaultCvars_;
            if (runJson.contains("cvars"))
            {
                MergeCVarMap(cvars, ParseCVarMap(runJson["cvars"]));
            }

            const std::string scenePath = runJson.value("scene", runJson.value("path", ""));
            const std::string renderer = runJson.value("renderer", "");
            const std::string label = runJson.value("name", runJson.value("label", ""));
            makeRun(scenePath, label, renderer, std::move(cvars));
        }
        return;
    }

    std::vector<std::pair<std::string, std::string>> scenes;
    if (root.contains("scenes") && root["scenes"].is_array())
    {
        for (const json& sceneJson : root["scenes"])
        {
            const std::string scenePath = ScenePathFromJson(sceneJson);
            if (!scenePath.empty())
            {
                scenes.push_back({scenePath, SceneLabelFromJson(sceneJson, scenePath)});
            }
        }
    }

    std::vector<std::string> renderers;
    if (root.contains("renderers") && root["renderers"].is_array())
    {
        for (const json& rendererJson : root["renderers"])
        {
            if (rendererJson.is_string())
            {
                renderers.push_back(rendererJson.get<std::string>());
            }
            else if (rendererJson.is_object())
            {
                renderers.push_back(rendererJson.value("name", rendererJson.value("renderer", "")));
            }
        }
    }
    if (renderers.empty())
    {
        renderers.push_back("");
    }

    for (const auto& [scenePath, sceneLabel] : scenes)
    {
        for (const std::string& renderer : renderers)
        {
            makeRun(scenePath, sceneLabel, renderer, defaultCvars_);
        }
    }
}

void BenchmarkGameInstance::BuildDefaultRuns(const Runtime::Config::Options& options)
{
    benchmarkRuns_.clear();

    std::vector<std::string> scenes;
    if (!options.SceneName.empty())
    {
        scenes.push_back(options.SceneName);
    }
    else
    {
        scenes = Runtime::Scene::SceneList::AllScenes;
    }

    for (const std::string& scene : scenes)
    {
        FBenchmarkRun run{};
        run.scene = scene;
        run.label = std::filesystem::path(scene).filename().replace_extension().string();
        run.cvars = defaultCvars_;
        benchmarkRuns_.push_back(std::move(run));
    }
}

void BenchmarkGameInstance::ApplyCurrentRunSettings()
{
    if (currentRunIndex_ >= benchmarkRuns_.size())
    {
        return;
    }

    auto& cvars = GetEngine().GetCVarSystem();
    for (const auto& [name, value] : benchmarkRuns_[currentRunIndex_].cvars)
    {
        std::string error;
        if (!cvars.SetValueFromString(name, value, NextCVar::ECVarSetBy::Console, &error))
        {
            SPDLOG_WARN("[Benchmark] Failed to set CVar {}={}: {}", name, value, error);
        }
    }
}

void BenchmarkGameInstance::LoadCurrentRun()
{
    if (currentRunIndex_ >= benchmarkRuns_.size())
    {
        GetEngine().RequestClose();
        return;
    }

    ApplyCurrentRunSettings();
    const FBenchmarkRun& run = benchmarkRuns_[currentRunIndex_];
    SPDLOG_INFO("[Benchmark] Loading run {}/{}: scene={} renderer={}",
                currentRunIndex_ + 1,
                benchmarkRuns_.size(),
                run.scene,
                run.rendererName.empty() ? "<current>" : run.rendererName);
    GetEngine().RequestLoadScene({.filename = run.scene});
}

bool BenchmarkGameInstance::AdvanceRun()
{
    currentRunIndex_++;
    if (currentRunIndex_ >= benchmarkRuns_.size())
    {
        return false;
    }
    LoadCurrentRun();
    return true;
}
