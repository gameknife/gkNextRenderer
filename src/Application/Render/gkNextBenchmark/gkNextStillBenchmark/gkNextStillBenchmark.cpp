#include "gkNextStillBenchmark.hpp"
#include "Common/BenchMark.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Modules/LDrawLoader/LDrawModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"
#include "Application/Common/DemoScenes.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    Modules::LDraw::Register();
    Modules::Scad::Register();
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
    
    // config.Width = 1920;
    // config.Height = 1080;
}

void BenchmarkGameInstance::ApplyDefaultCVars(NextCVar::FCVarSystem& cvars)
{
    std::string error;
    cvars.SetDefaultFromString("r.samples", "1", &error);
    cvars.SetDefaultFromString("r.temporalFrames", "2", &error);
    cvars.SetDefaultFromString("r.bounces", "4", &error);
    cvars.SetDefaultFromString("r.denoiser", "0", &error);
    cvars.SetDefaultFromString("r.superResolution", "4", &error);
}

void BenchmarkGameInstance::OnInit()
{
    benchMarker_ = std::make_unique<BenchMarker>();
    GetEngine().RequestLoadScene({.filename = Runtime::Scene::SceneList::AllScenes[0]});
}

void BenchmarkGameInstance::OnTick(double deltaSeconds)
{
    GetEngine().SetProgressiveRendering(true, true);
    if( benchMarker_ && benchMarker_->OnTick( GetEngine().GetWindow().GetTime(), &(GetEngine().GetRenderer()) ))
     {
         // Benchmark is done, report the results.
         benchMarker_->OnReport( &(GetEngine().GetRenderer()) , Runtime::Scene::SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex]);
         
         if (static_cast<size_t>(GetEngine().GetUserSettings().SceneIndex) ==
             Runtime::Scene::SceneList::AllScenes.size() - 1)
         {
             GetEngine().RequestClose();
         }
         else
         {
             GetEngine().GetUserSettings().SceneIndex += 1;
             GetEngine().RequestLoadScene({.filename = Runtime::Scene::SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex]});
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
