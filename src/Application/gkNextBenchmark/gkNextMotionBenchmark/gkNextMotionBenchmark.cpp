#include "gkNextMotionBenchmark.hpp"
#include "Application/gkNextBenchmark/Common/BenchMark.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/Config/CVarSystem.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options,
                                                         NextEngine* engine)
{
    return std::make_unique<BenchmarkGameInstance>(config, options, engine);
}

BenchmarkGameInstance::BenchmarkGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    config.Title = "gkNextMotionBenchmark";
    options.PresentMode = 0;
    options.Width = 1280;
    options.Height = 720;
    
    // config.Width = 1920;
    // config.Height = 1080;
}

void BenchmarkGameInstance::ApplyDefaultCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.samples", "8", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "16", &error);
    cvars.SetDefaultFromString("r.bounces", "4", &error);
    cvars.SetDefaultFromString("r.denoiser", "0", &error);
    cvars.SetDefaultFromString("r.superResolution", "4", &error);
}

void BenchmarkGameInstance::OnInit()
{
    benchMarker_ = std::make_unique<BenchMarker>();
    GetEngine().RequestLoadScene({.filename = SceneList::AllScenes[0]});
}

void BenchmarkGameInstance::OnTick(double deltaSeconds)
{
    if (benchMarker_ && benchMarker_->OnTick(GetEngine().GetWindow().GetTime(), &(GetEngine().GetRenderer())))
    {
        // Benchmark is done, report the results.
        benchMarker_->OnReport(&(GetEngine().GetRenderer()),
                               SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex]);

        if (static_cast<size_t>(GetEngine().GetUserSettings().SceneIndex) ==
            SceneList::AllScenes.size() - 1)
        {
            GetEngine().RequestClose();
        }
        else
        {
            GetEngine().GetUserSettings().SceneIndex += 1;
            GetEngine().RequestLoadScene({.filename = SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex]});
        }
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
