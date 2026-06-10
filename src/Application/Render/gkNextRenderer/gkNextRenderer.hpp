#pragma once
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/GameInstance.hpp"
#include "Modules/DevTools/GizmoController.hpp"
#include "Engine/Runtime/Camera/ModelViewController.hpp"

class NextRendererGameInstance : public NextGameInstanceBase
{
public:
    NextRendererGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
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
    float GetGraphicsDebugPanelTopOffset() const override;
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;
    bool OnGamepadInput(int16_t leftStickX, int16_t leftStickY,
                    int16_t rightStickX, int16_t rightStickY,
                    int16_t leftTrigger, int16_t rightTrigger) override;
    void ApplyDefaultCVars(NextCVar::FCVarSystem& cvars) override;

    void CreateSphereAndPush();
    void CreateBoxAndPush();

    enum class EWorkMode : uint8_t
    {
        Renderer = 0,
        Camera,
        World,
        Mesh,
        Profiler,
        Settings,
        Count,
    };

private:
    void DrawSettings();
    void DrawTitleBar();
    void DrawBottomStatusBar();
    void DrawModeRail();
    void DrawMemoryStatisticsPanel();
    void DrawViewportTopBar();
    void DrawViewportBottomBar();
    void RequestScreenshot(bool openFolder, const std::string& tag);
    Runtime::Camera::ModelViewController modelViewController_;
    NextUI::GizmoController gizmoController_;

    EWorkMode workMode_ = EWorkMode::Renderer;

    uint32_t modelId_;
    uint32_t boxModelId_;
    std::vector<uint32_t> matIds_;
    struct ImFont* bigFont_ {};
    struct ImFont* titleBarFont_ {};

    bool isTakingScreenshot_ = false;
    bool playbackPaused_ = false;
    bool stepRequested_ = false;
    bool memoryStatisticsPanelOpen_ = false;
};
