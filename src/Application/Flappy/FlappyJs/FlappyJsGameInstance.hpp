#pragma once

#include "Runtime/Engine.hpp"

class FlappyJsGameInstance final : public NextGameInstanceBase
{
public:
    FlappyJsGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~FlappyJsGameInstance() override = default;

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

private:
    NextEngine& GetEngine() const { return *engine_; }

    NextEngine* engine_ = nullptr;
};
