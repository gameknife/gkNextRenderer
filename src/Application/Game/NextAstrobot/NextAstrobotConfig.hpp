#pragma once

// ============================================================================
// NextAstrobotConfig.hpp - Every tunable number for NextAstrobot in one place
// (same convention as NextDayzConfig / CharacterDemoConfig). Defaults are the
// design's starting point; assets/configs/nextastrobot/gameplay.json overrides
// them, and assets/configs/nextastrobot/levels.json lists the level order.
// ============================================================================

#include <string>
#include <vector>

namespace NextAstrobot
{
    struct FMoveConfig
    {
        float RunSpeed = 6.0f;
        float RunAccel = 50.0f;
        float AirControl = 0.8f;
        float Gravity = 20.0f;
        float JumpSpeed = 8.9f;
        // Fraction of the remaining rise kept when the jump key is released early.
        float JumpCutMultiplier = 0.5f;
        float CoyoteSeconds = 0.12f;
        float JumpBufferSeconds = 0.15f;
        float HoverMaxSeconds = 1.0f;
        float HoverFallSpeed = -1.0f;
        float StompBounceSpeed = 5.0f;
        float PunchSeconds = 0.35f;
        float PunchRange = 1.2f;
        float PunchArcDegrees = 90.0f;
        float ControllerHeight = 1.5f;
        float ControllerRadius = 0.35f;
        float MaxStepHeight = 0.55f;
        float MaxSlopeDegrees = 50.0f;
        float DeathFadeSeconds = 0.8f;
        float TurnRateDegrees = 900.0f;
    };

    struct FCameraConfig
    {
        float Distance = 7.5f;
        float Height = 4.2f;
        float TargetHeight = 1.2f;
        float Fov = 45.0f;
        float Damping = 8.0f;
        float AutoYawRate = 1.5f;      // rad/s, camera drifting to face travel direction
        float ManualYawRate = 2.5f;    // rad/s, right stick / RMB drag
        float AutoYawIdleSeconds = 1.0f;
    };

    struct FWorldConfig
    {
        float PickupRadius = 0.9f;
        float HazardRadius = 0.9f;
        float EnemyRadius = 0.8f;
        float StompMargin = 0.2f;
        float InteractRadius = 1.2f;
        float RescueHoldSeconds = 0.5f;
        float CheckpointRadius = 1.6f;
        float GoalRadius = 3.0f;
        float EnemyPatrolHalfLength = 3.0f;
        float EnemyPatrolSpeed = 1.4f;
        float FlyerOrbitRadius = 1.8f;
        float FlyerOrbitSpeed = 0.8f;
        // Surface footing test: the foot must be inside the piece footprint and this
        // close to its top face (see design section 7.3).
        float FootContactTolerance = 0.15f;
    };

    struct FLevelDesc
    {
        std::string Id;
        std::string DisplayName;
        std::string Scene;
        std::string IntroCameraPath = "level-flythrough";
        std::string TitleCamera = "overview";
        // Death plane, relative to the lowest island surface found in the level.
        float KillPlaneOffset = -20.0f;
    };

    struct FConfig
    {
        FMoveConfig Move{};
        FCameraConfig Camera{};
        FWorldConfig World{};
        std::vector<FLevelDesc> Levels;

        /// Reads gameplay.json / levels.json; every field is optional and keeps its
        /// default. Returns false only when levels.json produced no playable level.
        bool Load(const std::string& gameplayPath, const std::string& levelsPath);
    };
}
