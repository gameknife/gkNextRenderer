#pragma once
#include "Common/CoreMinimal.hpp"
#include "Runtime/Engine.hpp"
#include "Runtime/ModelViewController.hpp"

class NextRendererGameInstance : public NextGameInstanceBase
{
public:
    NextRendererGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine);
    ~NextRendererGameInstance() override = default;

    // overrides
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override {};

    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
                       std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;

    void OnPreConfigUI() override;
    bool OnRenderUI() override;
    void OnInitUI() override;

    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;

    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;
    bool OnGamepadInput(int16_t leftStickX, int16_t leftStickY,
                    int16_t rightStickX, int16_t rightStickY,
                    int16_t leftTrigger, int16_t rightTrigger) override;

    // quick access engine
    NextEngine& GetEngine() { return *engine_; }

    void CreateSphereAndPush();
    void CreateBoxAndPush();

private:
    void DrawSettings();
    void DrawTitleBar();
    NextEngine* engine_;

    ModelViewController modelViewController_;

    uint32_t modelId_;
    uint32_t boxModelId_;
    std::vector<uint32_t> matIds_;
    struct ImFont* bigFont_ {};
};
