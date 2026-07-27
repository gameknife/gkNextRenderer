#pragma once

#include "Engine/Runtime/GameInstance.hpp"
#include "Gameplay/Camera/ModelViewController.hpp"

#include <memory>

namespace ScadLibrary
{
    class ScadLibraryInterface;
}

// SCAD parts library workbench: browse the kit_*.scad libraries under
// assets/scad/lib/, preview individual modules in the central viewport, and
// compose selected modules into a generated test scene (kit browser | viewport
// | scene assembly). Companion to ScadStudio, which generates .scad via AI.
class ScadLibraryGameInstance : public NextGameInstanceBase
{
public:
    ScadLibraryGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~ScadLibraryGameInstance() override;

    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    bool OnRenderUI() override;
    void OnPreConfigUI() override;
    void OnInitUI() override;
    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;

    // Orbit/zoom/pan the previewed parts in the central viewport.
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

private:
    std::unique_ptr<ScadLibrary::ScadLibraryInterface> ui_;
    Runtime::Camera::ModelViewController cameraController_;
};
