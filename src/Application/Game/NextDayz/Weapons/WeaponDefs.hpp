#pragma once

// ============================================================================
// WeaponDefs.hpp - Static weapon / ammo data tables (constexpr). MVP only needs
// the five weapons the riverland map scatters. Runtime state (mag counts, slots)
// lives in WeaponSystem; this file is pure data + lookup helpers.
// ============================================================================

#include <array>
#include <string_view>

namespace NextDayz
{
    enum class EAmmoType
    {
        Rifle545,
        Rifle762,
        Shotgun12,
        Pistol9,
        Count
    };

    // Inventory item id used to store this ammo as a stackable reserve.
    constexpr std::string_view AmmoItemId(EAmmoType type)
    {
        switch (type)
        {
        case EAmmoType::Rifle545:  return "ammo_545";
        case EAmmoType::Rifle762:  return "ammo_762";
        case EAmmoType::Shotgun12: return "ammo_12g";
        case EAmmoType::Pistol9:   return "ammo_9mm";
        default:                   return "ammo_unknown";
        }
    }

    constexpr std::string_view AmmoDisplayName(EAmmoType type)
    {
        switch (type)
        {
        case EAmmoType::Rifle545:  return "5.45x39";
        case EAmmoType::Rifle762:  return "7.62x54";
        case EAmmoType::Shotgun12: return "12 Gauge";
        case EAmmoType::Pistol9:   return "9x18";
        default:                   return "Ammo";
        }
    }

    struct FWeaponDef
    {
        std::string_view id;          // "ak" / "svd" / "mosin" / "shotgun" / "pistol"
        std::string_view displayName; // "AK-74"
        EAmmoType ammo;
        int   magSize;
        float fireInterval;           // seconds between shots (RPM converted)
        bool  fullAuto;
        float spreadHip;              // radians of random cone, hip fire
        float spreadAds;              // radians of random cone, aimed
        float adsFov;                 // absolute FOV target while aiming
        float cameraKickDegrees;      // upward camera velocity impulse
        float cameraYawDegrees;       // deterministic signed horizontal impulse range
        float viewModelKick;          // backward local offset velocity impulse
        float rigRecoilScale;         // TPS additive layer weight
    };

    inline constexpr std::array<FWeaponDef, 5> kWeapons = {{
        // id        display     ammo                    mag interval auto hip      ads      fov   camUp yaw vm   rig
        {"ak",      "AK-74",    EAmmoType::Rifle545,     30, 0.092f, true, 0.035f, 0.006f, 55.0f, 1.7f, 0.35f, 0.12f, 1.0f},
        {"svd",     "SVD",      EAmmoType::Rifle762,     10, 0.28f,  false,0.020f, 0.0015f,40.0f, 3.2f, 0.55f, 0.20f, 1.25f},
        {"mosin",   "Mosin",    EAmmoType::Rifle762,      5, 0.95f,  false,0.018f, 0.0012f,38.0f, 4.0f, 0.45f, 0.24f, 1.35f},
        {"shotgun", "Shotgun",  EAmmoType::Shotgun12,     6, 0.80f,  false,0.090f, 0.055f, 62.0f, 5.0f, 0.65f, 0.30f, 1.5f},
        {"pistol",  "Makarov",  EAmmoType::Pistol9,       12, 0.18f,  false,0.045f, 0.020f, 60.0f, 1.3f, 0.40f, 0.09f, 0.8f},
    }};

    // Returns nullptr for an unknown id.
    inline const FWeaponDef* FindWeaponDef(std::string_view id)
    {
        for (const FWeaponDef& def : kWeapons)
        {
            if (def.id == id)
            {
                return &def;
            }
        }
        return nullptr;
    }
}
