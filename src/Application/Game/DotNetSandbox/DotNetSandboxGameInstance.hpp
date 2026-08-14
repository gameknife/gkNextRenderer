#pragma once

#include "Engine/Runtime/GameInstance.hpp"

/// Minimal host for the managed scripting layer: it installs NextDotNet, loads an empty procedural
/// scene and forwards the lifecycle hooks. There is no gameplay here on purpose — this is the
/// target that proves the runtime works inside the real engine under both backends, and the shape
/// every C# game host follows.
class DotNetSandboxGameInstance final : public NextGameInstanceBase
{
public:
    DotNetSandboxGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~DotNetSandboxGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    bool OnKey(SDL_Event& event) override;
    bool OnMouseButton(SDL_Event& event) override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
};
