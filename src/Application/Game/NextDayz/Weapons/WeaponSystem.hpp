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
#include <vector>

#include <glm/glm.hpp>

#include "Application/Game/NextDayz/NextDayzConfig.hpp"
#include "Application/Game/NextDayz/Player/PlayerState.hpp"
#include "Application/Game/NextDayz/Weapons/WeaponDefs.hpp"

class NextEngine;

namespace Assets
{
    class Node;
}

namespace NextDayz
{
    struct FShotEvent
    {
        uint64_t sequence = 0;
        std::string weaponId;
        glm::vec2 cameraImpulseRadians{0.0f}; // x=yaw, y=pitch
        float viewModelImpulse = 0.0f;
        float rigRecoilScale = 1.0f;
    };

    class PlayerController;
    class Inventory;

    class WeaponSystem
    {
    public:
        void Configure(const FWeaponFeelConfig& config) { config_ = config; }

        // View-model proc assets injected by the GameInstance (BeforeSceneRebuild).
        void SetViewModelAssets(const std::array<uint32_t, kWeapons.size()>& modelIds,
                                const std::array<uint32_t, kWeapons.size()>& materialIds);

        void OnSceneLoaded(NextEngine& engine);
        void OnSceneUnloaded();

        // Loads a weapon into a slot with a fresh magazine. Returns false if id unknown.
        bool Equip(int slot, const std::string& weaponId);
        void SwitchSlot(int slot);
        void SwitchPrevious();

        void SetTriggerDown(bool down) { triggerDown_ = down; }
        void SetPresentationSuppressed(bool suppressed) { presentationSuppressed_ = suppressed; }
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
        bool IsSwitching() const { return switching_; }
        EWeaponPresentationAction PresentationAction() const;
        float PresentationActionTime() const;
        int ActiveSlot() const { return activeSlot_; }
        int SwitchTargetSlot() const { return switchTargetSlot_; }
        bool SwitchCommitted() const { return switchCommitted_; }
        const std::string& SlotWeaponId(int slot) const;

        std::vector<FShotEvent> ConsumeShotEvents();
        uint64_t LastShotSequence() const { return shotSequence_; }
        bool ViewModelRecoilActive() const;

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
        bool presentationSuppressed_ = false;
        bool triggerConsumed_ = false; // semi-auto: needs a release between shots
        float fireCooldown_ = 0.0f;
        bool reloading_ = false;
        float reloadTimer_ = 0.0f;
        bool switching_ = false;
        bool switchCommitted_ = false;
        int switchTargetSlot_ = -1;
        float switchTimer_ = 0.0f;
        uint64_t shotSequence_ = 0;
        std::vector<FShotEvent> shotEvents_;

        // view model
        std::array<uint32_t, kWeapons.size()> viewModelModelIds_{};
        std::array<uint32_t, kWeapons.size()> viewModelMaterialIds_{};
        bool viewModelAssetsSet_ = false;
        int viewModelWeaponIndex_ = -1;
        std::shared_ptr<Assets::Node> viewModelNode_;
        bool viewModelVisible_ = false;
        bool viewModelInScene_ = false;
        glm::vec3 viewModelOffset_{0.16f, -0.19f, 0.55f};
        float viewModelRecoil_ = 0.0f;
        float viewModelRecoilVelocity_ = 0.0f;

        std::mt19937 rng_{0xD00Du};
    };
}
