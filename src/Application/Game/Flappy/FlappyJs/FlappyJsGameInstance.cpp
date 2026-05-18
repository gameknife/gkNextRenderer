#include "Application/Game/Flappy/FlappyJs/FlappyJsGameInstance.hpp"

#include "Runtime/Subsystems/QuickJSEngine.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<FlappyJsGameInstance>(config, options, engine);
}

FlappyJsGameInstance::FlappyJsGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine) :
    NextGameInstanceBase(config, options, engine)
{
    ConfigureWindow(config, options, "FlappyJs", 1280, 720, true);
    options.QuickJSEntry = "assets/scripts/flappy/FlappyJs/FlappyJsGameInstance.js";
}

void FlappyJsGameInstance::OnInit()
{
    GetEngine().GetShowFlags().DebugGraphicsPanel = false;
    GetEngine().GetShowFlags().DebugPhysicsOverlay = false;
    GetEngine().GetUserSettings().ShowOverlay = false;
    if (QuickJSEngine* qjs = GetEngine().GetQuickJSEngine())
    {
        qjs->CallLifecycleHook("onInit");
    }
    GetEngine().RequestLoadScene({.filename = "Empty.proc"});
}

void FlappyJsGameInstance::OnTick(double) {}

void FlappyJsGameInstance::OnDestroy()
{
    if (QuickJSEngine* qjs = GetEngine().GetQuickJSEngine())
    {
        qjs->CallLifecycleHook("onDestroy");
    }
}

bool FlappyJsGameInstance::OnRenderUI()
{
    if (QuickJSEngine* qjs = GetEngine().GetQuickJSEngine())
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
    if (QuickJSEngine* qjs = GetEngine().GetQuickJSEngine())
    {
        qjs->CallBeforeSceneRebuild(nodes, models, materials, lights, tracks);
    }
}

void FlappyJsGameInstance::OnSceneLoaded()
{
    if (QuickJSEngine* qjs = GetEngine().GetQuickJSEngine())
    {
        qjs->CallLifecycleHook("onSceneLoaded");
    }
}

bool FlappyJsGameInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
{
    if (QuickJSEngine* qjs = GetEngine().GetQuickJSEngine())
    {
        return qjs->TryGetOverrideCamera(outRenderCamera);
    }
    return false;
}
