#include "Brotato3DCSharpGameInstance.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Modules/NextDotNet/NextDotNetModule.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                        Runtime::Config::Options& options,
                                                        NextEngine* engine)
{
    Modules::Scad::Register();
    return std::make_unique<Brotato3DCSharpGameInstance>(config, options, engine);
}

Brotato3DCSharpGameInstance::Brotato3DCSharpGameInstance(Vulkan::WindowConfig& config,
                                                         Runtime::Config::Options& options,
                                                         NextEngine* engine)
    : NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "Brotato3DCSharp", 1920, 1080, true);
    // Brotato owns thousands of node ids in managed pools. Reloading just the assembly without a
    // matching scene rebuild would create a half-reloaded world, so this target deliberately keeps
    // the simple restart-to-apply workflow. Hot reload is not a gameplay architecture requirement.
    Modules::NextDotNet::Install(*engine,
                                 {
                                     .gameAssembly = "brotato3d/Brotato3DCSharp.dll",
                                     .compileManagedSources = false,
                                     .enableHotReload = false,
                                 });
}

void Brotato3DCSharpGameInstance::OnInit()
{
    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().GetShowFlags().DebugPhysicsOverlay = false;
    GetEngine().GetUserSettings().ShowOverlay = false;
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnInit);
    }
}

void Brotato3DCSharpGameInstance::OnTick(double)
{
}

void Brotato3DCSharpGameInstance::OnDestroy()
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnDestroy);
    }
}

bool Brotato3DCSharpGameInstance::OnRenderUI()
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        return runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnRenderUI);
    }
    return false;
}

bool Brotato3DCSharpGameInstance::OnKey(SDL_Event&)
{
    return false;
}

bool Brotato3DCSharpGameInstance::OnMouseButton(SDL_Event&)
{
    return false;
}

bool Brotato3DCSharpGameInstance::OnGamepadInput(int16_t leftStickX,
                                                 int16_t leftStickY,
                                                 int16_t rightStickX,
                                                 int16_t rightStickY,
                                                 int16_t leftTrigger,
                                                 int16_t rightTrigger)
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->SetGamepadInput(leftStickX,
                                 leftStickY,
                                 rightStickX,
                                 rightStickY,
                                 leftTrigger,
                                 rightTrigger);
    }
    return false;
}

void Brotato3DCSharpGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
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

void Brotato3DCSharpGameInstance::OnSceneLoaded()
{
    if (auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        runtime->CallLifecycleHook(Modules::NextDotNet::EScriptHook::OnSceneLoaded);
    }
}

bool Brotato3DCSharpGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (const auto* runtime = Modules::NextDotNet::Get(GetEngine()))
    {
        return runtime->TryGetOverrideCamera(outRenderCamera);
    }
    return false;
}
