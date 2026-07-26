#pragma once

// ============================================================================
// NextDayzGameInstance.hpp - Orchestrates the NextDayz MVP: loads the coldwar
// map, owns the player/weapon/inventory/loot/time systems and routes input,
// camera and HUD. Business state lives in the sub-systems; this class only
// wires them together (see docs/projects/nextdayz/nextdayz-mvp-design.md).
// ============================================================================

#include "Engine/Runtime/GameInstance.hpp"

#include <glm/vec2.hpp>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"
#include "Application/Game/NextDayz/Player/PlayerActionController.hpp"
#include "Application/Game/NextDayz/Player/PlayerRigVisual.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponSystem.hpp"
#include "Application/Game/NextDayz/World/TimeSystem.hpp"

class NextDayzGameInstance : public NextGameInstanceBase
{
public:
    NextDayzGameInstance(Vulkan::WindowConfig& config, Runtime::Config::Options& options, NextEngine* engine);
    ~NextDayzGameInstance() override = default;

    // lifecycle
    void OnInit() override;
    void OnTick(double deltaSeconds) override;
    void OnDestroy() override;
    void ConfigureCVars(NextCVar::FCVarSystem& cvars) override;
    void RegisterAgentQueries(Runtime::Agent::FAgentQueryRegistry& reg) override;

    // scene
    void BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes, std::vector<Assets::Model>& models,
                            std::vector<Assets::FMaterial>& materials, std::vector<Assets::LightObject>& lights,
                            std::vector<Assets::AnimationTrack>& tracks) override;
    void OnSceneLoaded() override;
    void OnSceneUnloaded() override;

    // camera + ui
    bool OverrideRenderCamera(Assets::Camera& outRenderCamera) const override;
    bool OnRenderUI() override;
    bool ShouldRenderUiDuringScreenshot() const override { return true; }

    // input
    bool OnKey(SDL_Event& event) override;
    bool OnCursorPosition(double xpos, double ypos) override;
    bool OnMouseButton(SDL_Event& event) override;
    bool OnScroll(double xoffset, double yoffset) override;

private:
    glm::vec3 ResolveSpawnPosition() const;
    void EquipFromInventory(const std::string& weaponId, int slot);
    void ToggleClothing(const std::string& clothingId, bool on);
    void SetInventoryOpen(bool open);
    void SetMouseCaptured(bool captured);

    NextDayz::FConfig config_{};

    NextDayz::PlayerController player_;
    NextDayz::PlayerActionController actions_;
    NextDayz::PlayerRigVisual rig_;
    NextDayz::WeaponSystem weapons_;
    NextDayz::Inventory inventory_;
    NextDayz::LootSystem loot_;
    NextDayz::TimeSystem time_;

    // injected view-model proc assets
    uint32_t viewModelModelId_ = 0;
    uint32_t viewModelMaterialId_ = 0;

    bool sceneReady_ = false;
    bool showInventory_ = false;
    bool mouseCaptured_ = false;
    bool resetMouse_ = true;
    glm::dvec2 mousePos_{0.0, 0.0};
};
