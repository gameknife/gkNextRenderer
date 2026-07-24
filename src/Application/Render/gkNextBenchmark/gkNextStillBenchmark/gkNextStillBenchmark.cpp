#include "gkNextStillBenchmark.hpp"
#include "Common/BenchMark.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Assets/Loaders/LoaderRegistry.hpp"
#include "Application/Common/DemoScenes.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    AppCommon::RegisterDemoScenes();
    return std::make_unique<BenchmarkGameInstance>(config, options, engine);
}

BenchmarkGameInstance::BenchmarkGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    config.Title = "gkNextStillBenchmark";
    options.PresentMode = 0;
    options.Width = 1280;
    options.Height = 720;
    options.HighPrecisionProgressiveHistory = true;
}

void BenchmarkGameInstance::ConfigureCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.samples", "1", &error);
    cvars.SetDefaultFromString("r.upscaler.qualityMode", "4", &error);
}

void BenchmarkGameInstance::OnInit()
{
    benchMarker_ = std::make_unique<BenchMarker>();
    demoScenes_ = Assets::FLoaderRegistry::Get().ProcSceneNames();
    if (demoScenes_.empty())
    {
        SPDLOG_ERROR("[Benchmark] No DemoScenes are registered");
        GetEngine().RequestClose();
        return;
    }
    GetEngine().RequestLoadScene({.filename = demoScenes_.front()});
}

void BenchmarkGameInstance::OnTick(double deltaSeconds)
{
    GetEngine().SetProgressiveRendering(true);
    if( benchMarker_ && benchMarker_->OnTick( GetEngine().GetWindow().GetTime(), &(GetEngine().GetRenderer()) ))
     {
         // Benchmark is done, report the results.
         benchMarker_->OnReport(&(GetEngine().GetRenderer()), demoScenes_[currentSceneIndex_]);
         GetEngine().AddTickedTask([this](double) {
             if (GetEngine().IsCapturingScreenShot())
             {
                 return false;
             }

             currentSceneIndex_++;
             if (currentSceneIndex_ >= demoScenes_.size())
             {
                 GetEngine().RequestClose();
             }
             else
             {
                 GetEngine().RequestLoadScene({.filename = demoScenes_[currentSceneIndex_]});
             }
             return true;
         });
     }
}

void BenchmarkGameInstance::OnSceneLoaded()
{
    benchMarker_->OnSceneStart( GetEngine().GetWindow().GetTime() );
}

bool BenchmarkGameInstance::OnRenderUI()
{
    DrawBenchmarkStatsOverlay(GetEngine());
    return true;
}
