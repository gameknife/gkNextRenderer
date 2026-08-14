#pragma once

#include "Engine/Runtime/GameInstance.hpp"

/// Thin host for the C# Flappy implementation: it installs NextDotNet, points it at the
/// FlappyCSharp assembly and forwards the lifecycle hooks. All gameplay lives in
/// assets/csharp/Flappy/FlappyCSharp — keeping this file free of logic is what makes the parity
/// comparison against FlappyCpp meaningful.
class FlappyCSharpGameInstance final : public NextGameInstanceBase
{
public:
    FlappyCSharpGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~FlappyCSharpGameInstance() override = default;

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
