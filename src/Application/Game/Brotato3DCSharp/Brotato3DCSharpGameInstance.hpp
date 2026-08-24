#pragma once

#include "Engine/Runtime/GameInstance.hpp"

/// Thin native host for Brotato3DCSharp. Gameplay, scene pools and UI state all live in the
/// managed project; this class only installs NextDotNet and forwards engine lifecycle hooks.
class Brotato3DCSharpGameInstance final : public NextGameInstanceBase
{
public:
    Brotato3DCSharpGameInstance(Vulkan::WindowConfig& config,
                                Runtime::Config::Options& options,
                                NextEngine* engine);
    ~Brotato3DCSharpGameInstance() override = default;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    bool OnKey(SDL_Event& event) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnGamepadInput(int16_t leftStickX,
                        int16_t leftStickY,
                        int16_t rightStickX,
                        int16_t rightStickY,
                        int16_t leftTrigger,
                        int16_t rightTrigger) override;

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                            std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials,
                            std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
};
