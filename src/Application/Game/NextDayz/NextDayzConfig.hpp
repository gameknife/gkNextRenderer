#pragma once

// ============================================================================
// NextDayzConfig.hpp - All tunable numbers for the NextDayz MVP live here
// (mirrors the CharacterDemoConfig.hpp convention). No magic numbers scattered
// through the systems; balance and feel changes happen in one place.
// ============================================================================

#include <glm/vec3.hpp>

namespace NextDayz
{
    struct FPlayerConfig
    {
        float EyeHeight = 1.65f;        // camera height above controller feet (FPS)
        float WalkSpeed = 4.2f;         // m/s
        float RunSpeed = 7.6f;          // m/s (Shift)
        float AimMoveScale = 0.5f;      // speed multiplier while ADS
        float ControllerHeight = 1.8f;
        float ControllerRadius = 0.35f;
        float ControllerMass = 80.0f;
        float ControllerStrength = 4000.0f;
        float MaxStepHeight = 0.4f;
        float MaxSlopeAngle = 52.0f;
    };

    struct FCameraConfig
    {
        float MouseSensitivity = 0.0022f;
        float BaseFov = 75.0f;
        float FovLerpSpeed = 12.0f;     // exponential smoothing rate for ADS FOV
        float MaxPitchDegrees = 85.0f;
        // Third person
        float TpsDistance = 4.5f;
        float TpsHeight = 1.7f;
        float TpsMinDistance = 1.5f;
        float TpsMaxDistance = 9.0f;
        float ScrollStep = 0.6f;
    };

    struct FWeaponFeelConfig
    {
        // FPS view-model placement, in camera space (right, up, forward).
        glm::vec3 ViewModelHipOffset{0.22f, -0.30f, 0.5f};
        glm::vec3 ViewModelAdsOffset{0.0f, -0.075f, 0.42f};
        float ViewModelLerpSpeed = 14.0f;
        float TracerLifetimeSeconds = 0.04f;
        float FireAnimSeconds = 0.22f;  // TPS fire clip hold
    };

    struct FLootConfig
    {
        float ReachMeters = 3.0f;       // must be closer than this to interact
        float AimDotMin = 0.4f;         // crosshair-to-item alignment gate (cos angle)
        float SpawnRayHeight = 220.0f;  // downward raycast start height for spawn
    };

    struct FTimeConfig
    {
        double StartHour = 8.0;         // 08:00 first frame -> daytime
        double TimeScale = 1.0;         // 1 real second = 1 game minute (24 real-min day)
        float DaySunIntensity = 480.0f;
        float DaySkyIntensity = 120.0f;
        float NightSkyFraction = 0.12f; // sky never goes fully black
        float OvercastFactor = 0.55f;   // sun/sky multiplier while overcast
    };

    struct FConfig
    {
        FPlayerConfig Player{};
        FCameraConfig Camera{};
        FWeaponFeelConfig Weapon{};
        FLootConfig Loot{};
        FTimeConfig Time{};

        // Default spawn: bridge-west gas station (map centre, flat, stocked).
        // scad pad (-30,-177) -> engine XZ (x, -y) = (-30, 177). Ground Y is
        // resolved at runtime with a downward raycast (see PlayerController).
        glm::vec3 SpawnXZ{-30.0f, 0.0f, 177.0f};
    };
}
