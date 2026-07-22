#pragma once

// ============================================================================
// WeaponSystem.hpp - Two-slot weapon runtime for NextDayz: equip / switch /
// hitscan fire / reload / ADS. Fire is an engine RayCast (no damage in MVP,
// only visual feedback). A single FPS view-model node tracks the camera and
// lerps toward centre while aiming. Ammo reserves live in the Inventory.
// ============================================================================

#include <array>
#include <memory>
#include <random>
#include <string>

#include <glm/glm.hpp>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

class NextEngine;

namespace Assets
{
    class Node;
}

namespace NextDayz
{
    class PlayerController;
    class Inventory;

    class WeaponSystem
    {
    public:
        void Configure(const FWeaponFeelConfig& config) { config_ = config; }

        // View-model proc assets injected by the GameInstance (BeforeSceneRebuild).
        void SetViewModelAssets(uint32_t modelId, uint32_t materialId);

        void OnSceneLoaded(NextEngine& engine);
        void OnSceneUnloaded();

        // Loads a weapon into a slot with a fresh magazine. Returns false if id unknown.
        bool Equip(int slot, const std::string& weaponId);
        void SwitchSlot(int slot);
        void SwitchPrevious();

        void SetTriggerDown(bool down) { triggerDown_ = down; }
        void RequestReload(Inventory& inventory);

        void Update(float deltaSeconds, PlayerController& player, Inventory& inventory, NextEngine& engine);

        // --- queries (HUD / agent) ---
        bool HasActiveWeapon() const { return ActiveWeapon() != nullptr; }
        const FWeaponDef* ActiveWeapon() const;
        std::string ActiveWeaponId() const;
        std::string ActiveDisplayName() const;
        int AmmoInMag() const;
        int AmmoReserve(const Inventory& inventory) const;
        bool IsReloading() const { return reloading_; }
        int ActiveSlot() const { return activeSlot_; }
        const std::string& SlotWeaponId(int slot) const;

        // True on the frame a shot was fired (drives the TPS fire clip + recoil).
        bool ConsumeFiredThisFrame();

    private:
        struct FSlot
        {
            std::string weaponId;   // empty = empty slot
            int ammoInMag = 0;
        };

        void FireOneShot(PlayerController& player, NextEngine& engine);
        void RefreshViewModelVisibility();

        FWeaponFeelConfig config_{};
        std::array<FSlot, 2> slots_{};
        int activeSlot_ = 0;
        int previousSlot_ = 1;

        bool triggerDown_ = false;
        bool triggerConsumed_ = false; // semi-auto: needs a release between shots
        float fireCooldown_ = 0.0f;
        bool reloading_ = false;
        float reloadTimer_ = 0.0f;
        bool firedThisFrame_ = false;

        // view model
        uint32_t viewModelModelId_ = 0;
        uint32_t viewModelMaterialId_ = 0;
        bool viewModelAssetsSet_ = false;
        std::shared_ptr<Assets::Node> viewModelNode_;
        glm::vec3 viewModelOffset_{0.16f, -0.19f, 0.55f};

        std::mt19937 rng_{0xD00Du};
    };
}
