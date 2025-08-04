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
    void OnSceneLoaded() override;

    void OnPreConfigUI() override;
    bool OnRenderUI() override;
    void OnInitUI() override;

    bool OverrideRenderCamera(Assets::Camera& OutRenderCamera) const override;

    bool OnKey(int key, int scancode, int action, int mods) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(int button, int action, int mods) override;
    bool OnScroll(double xoffset, double yoffset) override;
    bool OnGamepadInput(float leftStickX, float leftStickY,
                    float rightStickX, float rightStickY,
                    float leftTrigger, float rightTrigger) override;
    
    // quick access engine
    NextEngine& GetEngine() { return *engine_; }

private:
    void DrawSettings();
    NextEngine* engine_;

    ModelViewController modelViewController_;
};
