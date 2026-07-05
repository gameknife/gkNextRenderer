#include "Engine/Runtime/GameInstance.hpp"
#include "FlappyJs/FlappyJsGameInstance.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextQuickJS/NextQuickJSModule.hpp"
#include "Modules/NextQuickJS/QuickJSEngine.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine)
{
    return std::make_unique<FlappyJsGameInstance>(config, options, engine);
}

FlappyJsGameInstance::FlappyJsGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "FlappyJs", 1280, 720, true);
    Modules::NextQuickJS::Install(
        *engine,
        {.entryScript = "assets/scripts/flappy/FlappyJs/FlappyJsGameInstance.js"});
}

void FlappyJsGameInstance::OnInit()
{
    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().GetShowFlags().DebugPhysicsOverlay = false;
    GetEngine().GetUserSettings().ShowOverlay = false;
    if (auto* qjs = Modules::NextQuickJS::Get(GetEngine()))
    {
        qjs->CallLifecycleHook("onInit");
    }
    GetEngine().RequestLoadScene({.filename = "Empty.proc"});
}

void FlappyJsGameInstance::OnTick(double) {}

void FlappyJsGameInstance::OnDestroy()
{
    if (auto* qjs = Modules::NextQuickJS::Get(GetEngine()))
    {
        qjs->CallLifecycleHook("onDestroy");
    }
}

bool FlappyJsGameInstance::OnRenderUI()
{
    if (auto* qjs = Modules::NextQuickJS::Get(GetEngine()))
    {
        qjs->CallLifecycleHook("onRenderUI");
    }
    return false;
}

bool FlappyJsGameInstance::OnKey(SDL_Event& event)
{
    (void)event;
    return false;
}

bool FlappyJsGameInstance::OnMouseButton(SDL_Event& event)
{
    (void)event;
    return false;
}

void FlappyJsGameInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                              std::vector<Assets::Model>& models,
                                              std::vector<Assets::FMaterial>& materials,
                                              std::vector<Assets::LightObject>& lights,
                                              std::vector<Assets::AnimationTrack>& tracks)
{
    if (auto* qjs = Modules::NextQuickJS::Get(GetEngine()))
    {
        qjs->CallBeforeSceneRebuild(nodes, models, materials, lights, tracks);
    }
}

void FlappyJsGameInstance::OnSceneLoaded()
{
    if (auto* qjs = Modules::NextQuickJS::Get(GetEngine()))
    {
        qjs->CallLifecycleHook("onSceneLoaded");
    }
}

bool FlappyJsGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (const auto* qjs = Modules::NextQuickJS::Get(GetEngine()))
    {
        return qjs->TryGetOverrideCamera(outRenderCamera);
    }
    return false;
}
