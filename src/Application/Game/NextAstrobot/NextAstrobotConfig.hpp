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
        float HoverMaxSeconds = 2.0f;
        float HoverFallSpeed = -1.0f;
        // Ascending boost at the start of hover: duration and total vertical climb.
        float HoverBoostSeconds = 1.0f;
        float HoverBoostHeight = 1.6f;
        float StompBounceSpeed = 5.0f;
        // Three-hit combo: left jab -> right cross -> spin kick. Each stage has its own
        // length; pressing again inside ComboWindowSeconds after a stage ends advances to
        // the next one, otherwise the next press starts over at the jab.
        float PunchSeconds = 0.30f;
        float PunchSeconds2 = 0.32f;
        float KickSeconds = 0.55f;
        float ComboWindowSeconds = 0.45f;
        // How long a press thrown mid-swing stays queued. Long enough to cover a whole
        // stage, because a combo the player has to time to the frame is one they mash at
        // and lose hits from.
        float ComboBufferSeconds = 0.45f;
        float PunchRange = 1.2f;
        float PunchArcDegrees = 90.0f;
        // The spin kick sweeps the whole circle, so it reaches further and hits behind.
        float KickRange = 1.9f;
        float KickArcDegrees = 360.0f;
        // Each hit carries the character forward a little, which is what makes a combo
        // feel like it lands rather than like three animations played on the spot.
        float PunchLungeSpeed = 3.4f;
        float KickLungeSpeed = 1.8f;
        // Skid stop: reversing at speed brakes first instead of flipping the velocity
        // through the run accel, so a hard turn reads as a slide with a plant.
        float SkidMinSpeed = 3.4f;
        float SkidSeconds = 0.34f;
        // Cosine between travel and wish direction under which a turn counts as a reversal.
        float SkidReverseDot = -0.35f;
        float SkidDecel = 26.0f;
        // The body keeps facing where it was going while the feet slide; that lag is the
        // whole read, so the turn rate is scaled down for the length of the skid.
        float SkidTurnScale = 0.3f;
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
        // Spring arm: the boom is pulled in when something solid sits between the player
        // and the camera, and eases back out once the view is clear again.
        float SpringArmRadius = 0.45f;        // keep-out sphere around the camera
        // Backed into a corner the boom can end up shorter than the character is wide, so
        // below this the rig is hidden rather than filling the lens.
        float SpringArmHideRigDistance = 1.0f;
        float SpringArmReturnRate = 7.0f;     // m/s the boom extends again once clear
        // Rescue close-up: the lens leaves the boom and orbits the freed robot while it
        // walks out and cheers. Blended in and out rather than cut, so the shot stays
        // continuous with the chase camera it came from and returns to.
        float FocusDistance = 4.0f;
        float FocusHeight = 1.9f;
        float FocusTargetHeight = 1.0f;
        float FocusFov = 38.0f;
        float FocusBlendSeconds = 0.45f;
        float FocusOrbitRate = 0.3f;          // rad/s the close-up drifts around the subject
        // How far off the player-to-subject line the close-up sits. Shooting straight down
        // that line puts the player's back across the lens, which is all you see.
        float FocusOffsetDegrees = 54.0f;
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
