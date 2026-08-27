#pragma once

#include <glm/glm.hpp>

#include <array>

namespace NextWorldTravel::Config
{
    // Loose output directory watched by the optional live geo-tile reload path.
    inline constexpr const char* kGeoSceneDirectory = "assets/geo";
    inline constexpr float kGeoSceneWatchIntervalSeconds = 0.5f;

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
    inline constexpr float kNearPlane = 1.0f;
    // Enough for a 1 km tile. An area is a grid of them, so the runtime scales
    // this (and the map-view distances below) by how big the loaded area
    // actually is — a 5 km area has a 7 km diagonal, and a far plane short of
    // it does not fade the far side out, it deletes it.
    inline constexpr float kFarPlane = 3000.0f;
    // The area size every constant tuned for one tile is expressed against.
    inline constexpr float kReferenceAreaM = 1000.0f;
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

    // ---- Aerial (bird's eye) ---------------------------------------------
    // The whole 1 km tile in frame with room around it; the camera is an orbit
    // around a pivot that pans over the ground, the way a map does.
    inline constexpr float kAerialDistance = 1150.0f;
    inline constexpr float kAerialMinDistance = 90.0f;
    inline constexpr float kAerialMaxDistance = 2800.0f;
    inline constexpr float kAerialPitch = -0.92f;
    inline constexpr float kAerialPitchMin = -1.44f;
    inline constexpr float kAerialPitchMax = -0.10f;
    // Panning is a fraction of the altitude per second, not a speed in metres:
    // at 1 km up the whole tile has to be reachable in a couple of seconds, at
    // 120 m up the same input has to stay usable for picking one block.
    inline constexpr float kAerialPanRate = 0.55f;
    inline constexpr float kAerialZoomRate = 0.16f;
    inline constexpr float kAerialKeyZoomRate = 0.9f;
    // Keeps the pivot inside the generated square; past the edge there is
    // nothing to look at but the skybox.
    inline constexpr float kAerialPivotRange = 620.0f;
    inline constexpr float kAerialGroundClearance = 30.0f;

    // ---- Focus orbit -----------------------------------------------------
    // Framing distance for a place: tall things need to be backed away from,
    // wide things need more than their own footprint.
    inline constexpr float kFocusMinRadius = 34.0f;
    inline constexpr float kFocusMaxRadius = 900.0f;
    inline constexpr float kFocusRadiusFromHeight = 1.45f;
    inline constexpr float kFocusRadiusFromFootprint = 1.25f;
    // Look at the middle of the mass rather than the base, so a tower fills the
    // frame instead of sitting in the bottom third of it.
    inline constexpr float kFocusCenterHeightFactor = 0.55f;
    inline constexpr float kFocusCenterMinLift = 8.0f;
    inline constexpr float kFocusPitch = -0.28f;
    inline constexpr float kFocusPitchMin = -1.32f;
    inline constexpr float kFocusPitchMax = -0.04f;
    inline constexpr float kFocusOrbitSpeed = 0.16f; // rad/s — a lap in ~40 s
    inline constexpr float kFocusOrbitSpeedMax = 0.7f;
    inline constexpr float kFocusZoomRate = 0.14f;
    inline constexpr float kFocusZoomMin = 0.45f;
    inline constexpr float kFocusZoomMax = 2.8f;
    inline constexpr float kFocusGroundClearance = 12.0f;
    // A 55 m station in a district of 300 m towers has no viewpoint at its own
    // scale: the orbit has to sit above the roofs around it and look down. The
    // roof height under the camera is measured with one downward ray, from high
    // enough to clear the tallest committed building (One WTC, 417 m).
    inline constexpr float kFocusSkylineClearance = 26.0f;
    inline constexpr float kSkylineProbeHeight = 700.0f;
    // Bearings sampled around the subject to find that skyline, and how far out
    // to sample as a fraction of the framing distance. Measured once per
    // subject: the neighbourhood does not change while the camera orbits it.
    inline constexpr int kSkylineBearings = 8;
    inline constexpr float kSkylineSampleFraction = 0.7f;
    // Steepest the lift is allowed to make the orbit before the framing
    // distance grows instead: past about 55 degrees a place stops being a
    // building you are looking at and becomes a floor plan.
    inline constexpr float kFocusMaxLiftSine = 0.82f;
    // Cap on that growth, so a low place next to a supertall still gets a shot
    // rather than a satellite photo.
    inline constexpr float kFocusLiftedMaxRadius = 1400.0f;
    // A neighbouring tower between the camera and the subject is the normal
    // case downtown. Shortening the boom would break the framing, so the orbit
    // climbs instead: these are the steps it may try, and how fast it settles.
    inline constexpr float kFocusOcclusionStep = 0.13f;
    inline constexpr int kFocusOcclusionSteps = 5;
    inline constexpr float kFocusOcclusionPadding = 4.0f;
    // Slow: the elevation correction runs while the orbit turns, and a camera
    // that snaps up every time a tower crosses the sight line is worse than the
    // occlusion it is avoiding.
    inline constexpr float kFocusPitchSharpness = 1.6f;
    // Auto-orbit stays paused for this long after the user lets go of the drag.
    inline constexpr float kOrbitResumeDelay = 1.6f;

    // ---- Tour ------------------------------------------------------------
    inline constexpr float kTourDwellSeconds = 11.0f;
    inline constexpr float kTourDwellMin = 3.0f;
    inline constexpr float kTourDwellMax = 45.0f;

    // ---- Mode transitions ------------------------------------------------
    // Cutting between a street and a bird's eye loses the viewer completely;
    // both of these are flown rather than snapped.
    inline constexpr float kModeBlendSeconds = 0.9f;
    inline constexpr float kFocusBlendSeconds = 1.4f;

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

    // ---- Aerial markers --------------------------------------------------
    // From above the tile is a map: every place gets a dot, but only the most
    // prominent get a name, otherwise the plates cover the city they describe.
    inline constexpr int kAerialMaxLabels = 44;
    inline constexpr float kAerialMarkerMaxDistance = 4200.0f;
    inline constexpr float kMarkerMinRadius = 2.0f;
    inline constexpr float kMarkerMaxRadius = 5.0f;
    inline constexpr float kMarkerAlpha = 0.7f;
    inline constexpr float kStreetMarkerRadius = 2.25f;
    inline constexpr float kMarkerPickRadius = 16.0f;
    inline constexpr float kMarkerHoverRadius = 4.0f;
    inline constexpr float kMarkerHoverThickness = 2.0f;
    inline constexpr float kLabelPaddingX = 10.0f;
    inline constexpr float kLabelPaddingY = 6.0f;
    inline constexpr float kLabelRounding = 7.0f;
    inline constexpr float kLabelHoverAlpha = 0.92f;
    inline constexpr float kLabelHoverThickness = 2.0f;
    inline constexpr float kLabelConnectorThickness = 1.0f;
    // Plates never overlap; one that would land on another is lifted by whole
    // rows until it finds a free one, and only dropped when every row is taken.
    // From a pavement most roofs project into the same band of sky, so without
    // the stacking a street view keeps about one label out of thirty.
    inline constexpr int kLabelStackRows = 5;
    inline constexpr float kLabelStackGap = 3.0f;
}
