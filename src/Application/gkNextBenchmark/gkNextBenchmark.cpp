#include "gkNextBenchmark.hpp"
#include "Runtime/Application.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine)
{
    return std::make_unique<BenchmarkGameInstance>(config, options, engine);
}

BenchmarkGameInstance::BenchmarkGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine):NextGameInstanceBase(config, options, engine), engine_(engine)
{
    config.Title = "gkNextBenchmark";
    options.Benchmark = true;
    options.BenchmarkNextScenes = true;
    options.Width = 1280;
    options.Height = 720;
}

void BenchmarkGameInstance::OnInit()
{
    benchMarker_ = std::make_unique<BenchMarker>();
    
}

void BenchmarkGameInstance::OnTick(double deltaSeconds)
{
    if( benchMarker_ && benchMarker_->OnTick( GetEngine().GetWindow().GetTime(), &(GetEngine().GetRenderer()) ))
     {
         // Benchmark is done, report the results.
         benchMarker_->OnReport( &(GetEngine().GetRenderer()) , SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex]);
         
         if (static_cast<size_t>(GetEngine().GetUserSettings().SceneIndex) ==
             SceneList::AllScenes.size() - 1)
         {
             GetEngine().RequestClose();
         }
         
         GetEngine().GetUserSettings().SceneIndex += 1;
         GetEngine().RequestLoadScene(SceneList::AllScenes[GetEngine().GetUserSettings().SceneIndex]);
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

bool BenchmarkGameInstance::OnKey(int key, int scancode, int action, int mods)
{
    return true;
}

bool BenchmarkGameInstance::OnCursorPosition(double xpos, double ypos)
{
    return true;
}

bool BenchmarkGameInstance::OnMouseButton(int button, int action, int mods)
{
    return true;
}
//
// void NextRendererApplication::TickBenchMarker()
// {
//     if( benchMarker_ && benchMarker_->OnTick( GetWindow().GetTime(), renderer_.get() ))
//     {
//         // Benchmark is done, report the results.
//         benchMarker_->OnReport(renderer_.get(), SceneList::AllScenes[userSettings_.SceneIndex]);
//         
//         if (!userSettings_.BenchmarkNextScenes || static_cast<size_t>(userSettings_.SceneIndex) ==
//             SceneList::AllScenes.size() - 1)
//         {
//             GetWindow().Close();
//         }
//         
//         userSettings_.SceneIndex += 1;
//         RequestLoadScene(SceneList::AllScenes[userSettings_.SceneIndex]);
//     }
// }