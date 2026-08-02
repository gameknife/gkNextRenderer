#pragma once

// ============================================================================
// NextDayzGameInstance.hpp - Orchestrates the NextDayz MVP: loads the coldwar
// map, owns the player/weapon/inventory/loot/time systems and routes input,
// camera and HUD. Business state lives in the sub-systems; this class only
// wires them together (see docs/projects/nextdayz/nextdayz-mvp-design.md).
// ============================================================================

#include "Engine/Runtime/GameInstance.hpp"
#include "Engine/Assets/Data/RigAsset.hpp"

#include <glm/vec2.hpp>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Data/DeterministicRng.hpp"
#include "Application/Game/NextDayz/Combat/CombatSystem.hpp"
#include "Application/Game/NextDayz/Combat/NoiseSystem.hpp"
#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Inventory/LootSystem.hpp"
#include "Application/Game/NextDayz/Player/PlayerController.hpp"
#include "Application/Game/NextDayz/Player/PlayerActionController.hpp"
#include "Application/Game/NextDayz/Player/PlayerRigVisual.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponSystem.hpp"
#include "Application/Game/NextDayz/World/TimeSystem.hpp"
#include "Application/Game/NextDayz/World/WorldAnchorRegistry.hpp"
#include "Application/Game/NextDayz/World/ZombieSpawnDirector.hpp"
#include "Application/Game/NextDayz/Zombies/ZombieVisualPool.hpp"
#include "Gameplay/AI/NavGrid.h"
#include "Gameplay/Rig/RigInstance.h"

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
    void UseInventoryItem(NextDayz::FItemInstanceId instanceId);
    void ApplyValidationLoadout();
    void CreateZombieVisuals();
    void SyncZombieVisuals(float deltaSeconds = 0.0f);
    void ConfigureZombieSpawnPoints();
    void BuildZombieNavigation();
    void ResetSession();
    std::string CurrentObjective() const;
    void SetInventoryOpen(bool open);
    void SetMouseCaptured(bool captured);

    NextDayz::FConfig config_{};
    NextDayz::FDeterministicRng sessionRng_;

    NextDayz::PlayerController player_;
    NextDayz::PlayerActionController actions_;
    NextDayz::SurvivalSystem survival_;
    NextDayz::PlayerRigVisual rig_;
    NextDayz::WeaponSystem weapons_;
    NextDayz::Inventory inventory_;
    NextDayz::LootSystem loot_;
    NextDayz::WorldAnchorRegistry worldAnchors_;
    NextDayz::ZombieSystem zombies_;
    NextDayz::CombatSystem combat_{zombies_};
    NextDayz::NoiseSystem noise_;
    NextDayz::ZombieSpawnDirector zombieSpawns_;
    NextDayz::ZombieVisualPool zombieVisuals_;
    NextGameplay::FNavGrid zombieNavGrid_;
    NextDayz::TimeSystem time_;

    // Injected weapon silhouettes shared by FPS view model and TPS attachment.
    std::array<uint32_t, NextDayz::kWeapons.size()> weaponModelIds_{};
    std::array<uint32_t, NextDayz::kWeapons.size()> weaponMaterialIds_{};
    uint32_t zombieModelId_ = 0;
    uint32_t zombieMaterialId_ = 0;
    Assets::FRigAsset zombieRigAsset_{};
    std::vector<uint32_t> zombieRigPartModelIds_;
    std::vector<std::array<uint32_t, 16>> zombieRigPartMaterialIds_;
    uint32_t zombieRigTintMaterialId_ = 0;
    bool zombieRigLoaded_ = false;
    struct FZombieRigVisual
    {
        std::shared_ptr<Assets::Node> worldNode;
        std::shared_ptr<Assets::Node> rigRoot;
        NextGameplay::FRigAnimator animator;
        bool visible = false;
    };
    std::vector<FZombieRigVisual> zombieRigVisuals_;
    std::vector<std::shared_ptr<Assets::Node>> zombieNodes_;
    std::vector<NextDayz::FZombieHandle> zombieVisualOwners_;

    bool sceneReady_ = false;
    bool showInventory_ = false;
    bool showDebugPanel_ = false;
    bool validationLoadout_ = false;
    bool validationAddBackpack_ = false;
    bool validationAddSupplies_ = false;
    bool validationSpawnZombie_ = false;
    bool validationUseWater_ = false;
    bool validationFinishZombie_ = false;
    bool validationRestart_ = false;
    float validationDamagePlayer_ = 0.0f;
    float validationHunger_ = -1.0f;
    float validationHydration_ = -1.0f;
    uint64_t validationAttackSequence_ = 0;
    bool paused_ = false;
    double survivalSeconds_ = 0.0;
    NextDayz::FWorldAnchorHandle hoveredWell_{};
    bool mouseCaptured_ = false;
    bool resetMouse_ = true;
    glm::dvec2 mousePos_{0.0, 0.0};
};
