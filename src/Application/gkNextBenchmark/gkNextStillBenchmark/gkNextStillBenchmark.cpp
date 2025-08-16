#include "gkNextStillBenchmark.hpp"
#include "Application/gkNextBenchmark/Common/BenchMark.hpp"
#include "Runtime/Engine.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<BenchmarkGameInstance>(config, options, engine);
}

BenchmarkGameInstance::BenchmarkGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine):NextGameInstanceBase(config, options, engine), engine_(engine)
{
    config.Title = "gkNextStillBenchmark";
    options.Samples = 1;
    options.Temporal = 2;
    options.Bounces = 4;
    options.PresentMode = 0;
    options.NoDenoiser = true;
    options.Width = 1280;
    options.Height = 720;
    options.SuperResolution = 2;
}

void BenchmarkGameInstance::OnInit()
{
    benchMarker_ = std::make_unique<BenchMarker>();
    GetEngine().RequestLoadScene(SceneList::AllScenes[0]);
}

void BenchmarkGameInstance::OnTick(double deltaSeconds)
{
    GetEngine().SetProgressiveRendering(true, true);
    if( benchMarker_ && benchMarker_->OnTick( GetEngine().GetWindow().GetTime(), &(GetEngine().GetRenderer()) ))
     {
         // Benchmark is done, report the results.
         benchMarker_->OnReport( &(GetEngine().GetRenderer()) , SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex]);
         
         if (static_cast<size_t>(GetEngine().GetUserSettings().SceneIndex) ==
             SceneList::AllScenes.size() - 1)
         {
             GetEngine().RequestClose();
         }
         else
         {
             GetEngine().GetUserSettings().SceneIndex += 1;
             GetEngine().RequestLoadScene(SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex]);
         }
     }
}

void BenchmarkGameInstance::OnSceneLoaded()
{
    benchMarker_->OnSceneStart( GetEngine().GetWindow().GetTime() );
}

bool BenchmarkGameInstance::OnRenderUI()
{
    return true;
}
