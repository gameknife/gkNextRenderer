#pragma once

// ============================================================================
// NextDayzHUD.hpp - Minimal ImGui HUD: crosshair, weapon/ammo readout, clock,
// interaction prompt and the (toggleable) inventory panel. Pure presentation:
// the GameInstance packs a context each frame and receives equip/wear requests
// back through callbacks.
// ============================================================================

#include <functional>
#include <cstdint>
#include <array>
#include <string>

#include <glm/glm.hpp>

#include "Application/Game/NextDayz/Inventory/Inventory.hpp"
#include "Application/Game/NextDayz/Player/SurvivalSystem.hpp"

namespace NextDayz
{
    class WeaponSystem;

    struct FDebugHudState
    {
        glm::vec3 position{0.0f};
        glm::vec3 eyePosition{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec2 localMove{0.0f};
        glm::vec2 cameraRecoilRadians{0.0f};
        float yawRadians = 0.0f;
        float pitchRadians = 0.0f;
        float fovDegrees = 0.0f;
        float horizontalSpeed = 0.0f;
        float controllerHeight = 0.0f;
        float aimWeight = 0.0f;
        float actionTime = 0.0f;
        std::string stance;
        std::string desiredStance;
        std::string gait;
        std::string jumpPhase;
        std::string traversalAction;
        std::string traversalProbeResult;
        std::string baseAnimation;
        std::string action;
        std::string weaponAction;
        std::string weaponActionClip;
        float weaponActionTime = 0.0f;
        float weaponActionWeight = 0.0f;
        float jumpPhaseTime = 0.0f;
        float traversalTime = 0.0f;
        float traversalHeight = 0.0f;
        int switchTargetSlot = -1;
        bool onGround = false;
        bool standBlocked = false;
        bool sprinting = false;
        bool actionCommitted = false;
        bool cameraRecoilActive = false;
        bool rigRecoilActive = false;
        bool viewModelRecoilActive = false;
        bool switchingWeapon = false;
        bool switchCommitted = false;
        uint64_t shotSequence = 0;
        int activeZombies = 0;
        int alertedZombies = 0;
        int zombieKills = 0;
        int zombiePathSegments = 0;
        int hitProxyRegistered = 0;
        int hitProxyCpuEligible = 0;
        int lootAvailable = 0;
        int lootReserved = 0;
        int lootCooldown = 0;
        std::array<int, 6> lootAvailableByCategory{};
        std::array<int, 6> lootTotalByCategory{};
        int criticalFood = 0;
        int criticalMedical = 0;
        int criticalBackpack = 0;
        int criticalWeapons = 0;
        int criticalAmmo = 0;
        int criticalWaterSources = 0;
        int recentWeaponTraces = 0;
        uint32_t lastTraceInstanceId = 0;
        std::string lastTraceResult;
        bool zombieOverlay = true;
        bool lootOverlay = true;
        bool hitProxyOverlay = true;
    };

    struct FHudContext
    {
        bool aiming = false;
        bool firstPerson = true;

        bool hasWeapon = false;
        std::string weaponName;
        int ammoInMag = 0;
        int ammoReserve = 0;
        bool reloading = false;
        int activeSlot = 0;

        std::string interactionPrompt; // empty = nothing hovered
        int hour = 8;
        int minute = 0;
        bool overcast = false;

        bool showInventory = false;
        bool showDebugPanel = false;
        const Inventory* inventory = nullptr;
        const WeaponSystem* weapons = nullptr;
        FSurvivalSnapshot survival;
        bool paused = false;
        double survivalSeconds = 0.0;
        std::string objective;
        FDebugHudState debug;

        std::function<void(const std::string& weaponId, int slot)> equipWeapon;
        std::function<void(const std::string& clothingId, bool on)> toggleClothing;
        std::function<void(FItemInstanceId instanceId)> useItem;
        std::function<void()> restartSession;
    };

    namespace NextDayzHUD
    {
        void Draw(const FHudContext& context);
    }
}
