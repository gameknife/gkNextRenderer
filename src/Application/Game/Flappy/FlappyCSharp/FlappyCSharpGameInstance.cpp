#include "FlappyCSharp/FlappyCSharpGameInstance.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Modules/NextDotNet/NextDotNetModule.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                        Runtime::Config::Options& options,
                                                        NextEngine* engine)
{
    return std::make_unique<FlappyCSharpGameInstance>(config, options, engine);
}

FlappyCSharpGameInstance::FlappyCSharpGameInstance(Vulkan::WindowConfig& config,
                                                   Runtime::Config::Options& options,
                                                   NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "FlappyCSharp", 1280, 720, true);
    Modules::NextDotNet::Install(*engine, {.gameAssembly = "flappy/FlappyCSharp.dll"});
}

void FlappyCSharpGameInstance::OnInit()
{
    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().GetShowFlags().DebugPhysicsOverlay = false;
    GetEngine().GetUserSettings().ShowOverlay = false;

    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnInit);
    }
    GetEngine().RequestLoadScene({.filename = "Empty.proc"});
}

void FlappyCSharpGameInstance::OnTick(double)
{
}

void FlappyCSharpGameInstance::OnDestroy()
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnDestroy);
    }
}

bool FlappyCSharpGameInstance::OnRenderUI()
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        return runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnRenderUI);
    }
    return false;
}

bool FlappyCSharpGameInstance::OnKey(SDL_Event&)
{
    return false;
}

bool FlappyCSharpGameInstance::OnMouseButton(SDL_Event&)
{
    return false;
}

void FlappyCSharpGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                                  std::vector<Assets::Model>& models,
                                                  std::vector<Assets::FMaterial>& materials,
                                                  std::vector<Assets::LightObject>& lights,
                                                  std::vector<Assets::AnimationTrack>& tracks)
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->CallBeforeSceneRebuild(nodes, models, materials, lights, tracks);
    }
}

void FlappyCSharpGameInstance::OnSceneLoaded()
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnSceneLoaded);
    }
}

bool FlappyCSharpGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (const auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        return runtime->TryGetOverrideCamera(outRenderCamera);
    }
    return false;
}
