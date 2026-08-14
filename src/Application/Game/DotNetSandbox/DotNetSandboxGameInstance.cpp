#include "DotNetSandboxGameInstance.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Modules/NextDotNet/NextDotNetModule.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                        Runtime::Config::Options& options,
                                                        NextEngine* engine)
{
    return std::make_unique<DotNetSandboxGameInstance>(config, options, engine);
}

DotNetSandboxGameInstance::DotNetSandboxGameInstance(Vulkan::WindowConfig& config,
                                                     Runtime::Config::Options& options,
                                                     NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "DotNetSandbox", 1280, 720, true);
    Modules::NextDotNet::Install(*engine, {});
}

void DotNetSandboxGameInstance::OnInit()
{
    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().GetUserSettings().ShowOverlay = false;

    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnInit);
    }

    // Empty.proc gives the script a scene to build into via the SceneBuild bindings, without any
    // authored content getting in the way of what the managed side produced.
    GetEngine().RequestLoadScene({.filename = "Empty.proc"});
}

void DotNetSandboxGameInstance::OnTick(double)
{
}

void DotNetSandboxGameInstance::OnDestroy()
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnDestroy);
    }
}

bool DotNetSandboxGameInstance::OnRenderUI()
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        return runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnRenderUI);
    }
    return false;
}

bool DotNetSandboxGameInstance::OnKey(SDL_Event&)
{
    return false;
}

bool DotNetSandboxGameInstance::OnMouseButton(SDL_Event&)
{
    return false;
}

void DotNetSandboxGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
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

void DotNetSandboxGameInstance::OnSceneLoaded()
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnSceneLoaded);
    }
}

bool DotNetSandboxGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (const auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        return runtime->TryGetOverrideCamera(outRenderCamera);
    }
    return false;
}
