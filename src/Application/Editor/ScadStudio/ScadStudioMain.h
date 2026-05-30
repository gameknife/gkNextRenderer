#pragma once

#include "Engine/Runtime/Camera/ModelViewController.hpp"
#include "Engine/Runtime/GameInstance.hpp"

#include <memory>

namespace ScadStudio
{
    class ScadStudioInterface;
}

// Conversational SCAD model generator. A minimal three-pane authoring app
// (sessions + structure tree | viewport | chat) built on the engine's
// NextGameInstanceBase + AI service + OpenSCAD loader.
class ScadStudioGameInstance : public NextGameInstanceBase
{
public:
    ScadStudioGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~ScadStudioGameInstance() override;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    void OnPreConfigUI() override;
    void OnInitUI() override;
    void OnSceneLoaded() override;
    void ApplyDefaultCVars(NextCVar::FCVarSystem& cvars) override;

    // Orbit/zoom/pan the model in the central viewport.
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

private:
    std::unique_ptr<ScadStudio::ScadStudioInterface> ui_;
    Runtime::Camera::ModelViewController cameraController_;
    bool agentValidationCaptured_ = false;
};
