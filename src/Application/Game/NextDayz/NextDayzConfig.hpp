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
        float StandingEyeHeight = 1.65f;
        float CrouchedEyeHeight = 1.02f;
        float EyeHeightLerpSpeed = 12.0f;
        float StandWalkSpeed = 2.0f;
        float StandRunSpeed = 4.2f;
        float StandSprintSpeed = 7.6f;
        float CrouchWalkSpeed = 1.55f;
        float BackwardScale = 0.72f;
        float StrafeScale = 0.82f;
        float AimMoveScale = 0.65f;
        float StandingHeight = 1.8f;
        float CrouchedHeight = 1.18f;
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
        float TpsDistance = 3.8f;
        float TpsHeight = 1.7f;
        float TpsShoulderOffset = 0.90f;
        float TpsMinDistance = 1.5f;
        float TpsMaxDistance = 9.0f;
        float ScrollStep = 0.6f;
    };

    struct FWeaponFeelConfig
    {
        // FPS view-model placement, in camera space (right, up, forward).
        float ViewModelScale = 0.65f;
        glm::vec3 ViewModelHipOffset{0.22f, -0.30f, 0.75f};
        glm::vec3 ViewModelAdsOffset{0.0f, -0.060f, 0.75f};
        float ViewModelLerpSpeed = 14.0f;
        float CameraRecoilSpring = 90.0f;
        float CameraRecoilDamping = 18.0f;
        float ViewModelRecoilSpring = 120.0f;
        float ViewModelRecoilDamping = 20.0f;
        float TracerLifetimeSeconds = 0.04f;
        float FireAnimSeconds = 0.22f;  // TPS fire clip hold
        float ReloadSeconds = 2.2f;
        float SwitchSeconds = 0.9f;
    };

    struct FAnimationConfig
    {
        float LocomotionFadeSeconds = 0.14f;
        float MoveThreshold = 0.15f;
        float StandWalkAuthoredSpeed = 2.0f;
        float StandRunAuthoredSpeed = 4.2f;
        float StandSprintAuthoredSpeed = 7.6f;
        float CrouchWalkAuthoredSpeed = 1.55f;
        float MinPlayRate = 0.75f;
        float MaxPlayRate = 1.35f;
        float WeaponReadyPoseWeight = 1.0f;
        float AimFadeSeconds = 0.12f;
        float AimPitchLimitDegrees = 80.0f;
        float RecoilFadeOutSeconds = 0.04f;
        float JumpUpSeconds = 0.24f;
        float JumpDownSeconds = 0.32f;
        float JumpFadeSeconds = 0.08f;
    };

    struct FTraversalConfig
    {
        float ProbeDistance = 1.15f;
        float ProbeHeight = 0.52f;
        float MinObstacleHeight = 0.32f;
        float VaultMaxHeight = 0.88f;
        float ClimbMaxHeight = 1.48f;
        float TopProbeInset = 0.12f;
        float MaxVaultDepth = 0.95f;
        float PlatformStandDepth = 0.32f;
        float SurfaceNormalMinY = 0.70f;
        float StandingClearance = 0.10f;
        float ArcClearance = 0.14f;
        float VaultDurationSeconds = 0.52f;
        float ClimbDurationSeconds = 0.86f;
    };

    struct FLootConfig
    {
        float ReachMeters = 3.0f;       // must be closer than this to interact
        float AimDotMin = 0.4f;         // crosshair-to-item alignment gate (cos angle)
        float SpawnRayHeight = 220.0f;  // downward raycast start height for spawn
    };

    struct FActionConfig
    {
        float LootDurationSeconds = 0.9f;
        float LootCommitNormalizedTime = 0.55f;
        float ActionFadeSeconds = 0.10f;
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
        FAnimationConfig Animation{};
        FTraversalConfig Traversal{};
        FLootConfig Loot{};
        FActionConfig Action{};
        FTimeConfig Time{};

        // Default spawn: bridge-west gas station (map centre, flat, stocked).
        // scad pad (-30,-177) -> engine XZ (x, -y) = (-30, 177). Ground Y is
        // resolved at runtime with a downward raycast (see PlayerController).
        glm::vec3 SpawnXZ{-30.0f, 0.0f, 177.0f};
    };
}
