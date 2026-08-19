#pragma once

#include <glm/glm.hpp>

#include <array>

namespace GeoWalk::Config
{
    // ---- Character -------------------------------------------------------
    inline constexpr const char* kRigPath = "assets/scad/characters/citizen.scad";
    inline constexpr float kWalkSpeed = 1.7f;
    inline constexpr float kRunSpeed = 4.6f;
    inline constexpr float kAgentRadius = 0.3f;
    inline constexpr float kCharacterHeight = 1.75f;
    inline constexpr glm::vec3 kCharacterTint{0.86f, 0.34f, 0.22f};

    // ---- Navigation ------------------------------------------------------
    // A whole 1 km tile at walking resolution is a million raycast columns, so
    // the nav grid is a window that slides with the walker (design §6b).
    inline constexpr float kNavCellSize = 0.7f;
    inline constexpr float kNavWindowHalfSize = 130.0f;
    // Rebuild once the walker gets this close to the window's edge. Has to be
    // comfortably more than one A* path length so a route never runs off the
    // grid mid-walk.
    inline constexpr float kNavRebuildMargin = 45.0f;
    inline constexpr float kNavMaxStepHeight = 0.55f;
    inline constexpr float kNavClearanceHeight = 1.9f;
    inline constexpr float kNavMaxSlopeAngle = 50.0f;
    // floorHeightTolerance is an absolute band, so it must span the window's
    // own relief; this is added on top of the measured min/max spread.
    inline constexpr float kNavFloorToleranceSlack = 12.0f;
    // 0 = automatic sceneMax.y + 5. Adapts to any tile regardless of elevation.
    inline constexpr float kNavSampleCeiling = 0.0f;

    // ---- Roaming ---------------------------------------------------------
    inline constexpr float kRoamMinDistance = 25.0f;
    inline constexpr float kRoamMaxDistance = 95.0f;
    inline constexpr int kRoamTargetAttempts = 48;
    // Give up on a destination that has not been reached in this long; a nav
    // cell can be walkable and still be behind a gap the follower cannot cross.
    inline constexpr float kRoamTimeoutSeconds = 45.0f;
    inline constexpr float kRoamPauseSeconds = 1.6f;

    // ---- Camera ----------------------------------------------------------
    inline constexpr float kFov = 55.0f;
    inline constexpr float kNearPlane = 0.1f;
    inline constexpr float kFarPlane = 3000.0f;
    inline constexpr float kFollowDistance = 9.0f;
    inline constexpr float kFollowMinDistance = 2.0f;
    inline constexpr float kFollowMaxDistance = 90.0f;
    // Well above head height: the boom then clears parked geometry and street
    // furniture, and looking slightly down shows the street the character is on.
    inline constexpr float kFollowHeight = 2.6f;
    inline constexpr float kSpawnPitch = -0.22f;
    // Above the tallest committed building (One WTC, 417 m) so the snap-to-tile
    // view clears the skyline of every generated tile rather than just this one.
    inline constexpr float kOverviewHeight = 620.0f;
    inline constexpr float kOverviewSetback = 480.0f;
    inline constexpr float kOverviewPitch = -0.85f;
    inline constexpr float kCameraCollisionPadding = 0.35f;
    // The occlusion ray starts this far down the boom, clear of the character's
    // own rig geometry.
    inline constexpr float kCameraCollisionStart = 0.9f;
    inline constexpr float kFreeFlySpeed = 60.0f;
    inline constexpr float kMouseSensitivity = 0.0032f;
    inline constexpr float kCameraSharpness = 12.0f;

    // ---- POI labels ------------------------------------------------------
    // How many labels may be on screen at once. The sidecar is rank-sorted, so
    // the cap keeps the landmarks and drops the anonymous named blocks.
    inline constexpr int kMaxVisibleLabels = 28;
    inline constexpr float kLabelMaxDistance = 700.0f;
    inline constexpr float kLabelFadeStart = 420.0f;
    // Buildings get their label at the roof; everything else floats this high
    // over the ground so a label is not lost in the street furniture.
    inline constexpr float kLabelGroundOffset = 6.0f;
    inline constexpr float kLabelRoofOffset = 4.0f;
    // Below this rank a place only shows when you are close to it.
    inline constexpr float kLabelMinorRank = 6.0f;
    inline constexpr float kLabelMinorMaxDistance = 130.0f;
}
