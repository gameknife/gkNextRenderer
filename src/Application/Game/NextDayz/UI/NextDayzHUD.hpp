#pragma once

// ============================================================================
// NextDayzHUD.hpp - Minimal ImGui HUD: crosshair, weapon/ammo readout, clock,
// interaction prompt and the (toggleable) inventory panel. Pure presentation:
// the GameInstance packs a context each frame and receives equip/wear requests
// back through callbacks.
// ============================================================================

#include <functional>
#include <string>

namespace NextDayz
{
    class Inventory;
    class WeaponSystem;

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
        const Inventory* inventory = nullptr;
        const WeaponSystem* weapons = nullptr;

        std::function<void(const std::string& weaponId, int slot)> equipWeapon;
        std::function<void(const std::string& clothingId, bool on)> toggleClothing;
    };

    namespace NextDayzHUD
    {
        void Draw(const FHudContext& context);
    }
}
