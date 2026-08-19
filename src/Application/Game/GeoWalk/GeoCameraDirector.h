#pragma once

#include "GeoWalkConfig.hpp"

#include <glm/glm.hpp>

#include <functional>

namespace Runtime { class TerrainComponent; }

namespace GeoWalk
{
    // The three ways this application looks at a generated tile. Walking is one
    // of them, not the whole application: Aerial is the map you pick a place
    // from, Focus is the orbit that shows you the place you picked.
    enum class EViewMode
    {
        Walk,   // over the shoulder of the character
        Aerial, // bird's eye over the tile, every place drawn as a marker
        Focus   // orbit one place
    };

    // Sub-camera of Walk mode.
    enum class EWalkCamera
    {
        Follow, // orbits the character
        Free    // detached, WASD flies the camera
    };

    struct FCameraPose
    {
        glm::vec3 eye{0.0f, 240.0f, 460.0f};
        glm::vec3 target{0.0f};
    };

    struct FCameraMoveInput
    {
        float forward = 0.0f; // -1 .. 1
        float right = 0.0f;
        float up = 0.0f;
        bool sprint = false;
    };

    // Everything the director needs to know about the world it flies through.
    // The application owns the scene, so it supplies the ray probe rather than
    // the director reaching into the engine itself.
    struct FCameraWorld
    {
        glm::vec3 walkerPosition{0.0f};
        bool walkerValid = false;
        const Runtime::TerrainComponent* terrain = nullptr;
        // Distance from `origin` along `direction` to the first scene hit, or a
        // negative value when the ray is clear.
        std::function<float(const glm::vec3& origin, const glm::vec3& direction)> probe;
    };

    // The place Focus mode orbits.
    struct FFocusSubject
    {
        glm::vec3 center{0.0f}; // what the camera looks at
        float radius = 40.0f;   // framing distance, before the user's zoom
        float halfExtent = 8.0f; // the subject's own horizontal half-size, so an
                                 // occlusion ray can tell the subject apart from
                                 // whatever is standing in front of it
        bool valid = false;
    };

    // Owns every camera in the application and the transitions between them.
    //
    // Each mode computes an exact pose from its own state — mouse look must not
    // lag — and a mode or subject change flies from the pose that was on screen
    // at the moment of the switch to the new one. Cutting from a pavement to a
    // bird's eye loses the viewer entirely; a blend keeps the tile oriented.
    class FGeoCameraDirector
    {
    public:
        void Tick(float deltaSeconds, const FCameraWorld& world);

        // ---- Modes --------------------------------------------------------
        void SetMode(EViewMode mode, const FCameraWorld& world);
        EViewMode Mode() const { return mode_; }
        void SetWalkCamera(EWalkCamera camera);
        void ToggleWalkCamera() { SetWalkCamera(walkCamera_ == EWalkCamera::Follow ? EWalkCamera::Free
                                                                                   : EWalkCamera::Follow); }
        EWalkCamera WalkCamera() const { return walkCamera_; }

        // ---- Focus --------------------------------------------------------
        // Re-aims the orbit. The new orbit starts from the camera's current
        // bearing so the subject does not swing across the screen first.
        void SetFocusSubject(const FFocusSubject& subject);
        bool& AutoOrbit() { return autoOrbit_; }
        bool AutoOrbit() const { return autoOrbit_; }
        float& OrbitSpeed() { return orbitSpeed_; }
        float OrbitSpeed() const { return orbitSpeed_; }

        // ---- Input --------------------------------------------------------
        void AddLook(float dx, float dy);
        void AddZoom(float wheel);
        void SetMoveInput(const FCameraMoveInput& input) { move_ = input; }
        void SetLookActive(bool active);
        // Aims the walk camera at a world point (the HUD's "look at").
        void LookAt(const glm::vec3& point);
        // Sets the walk heading outright, for the opening shot of a tile.
        void SetHeading(float yaw, float pitch);

        // ---- Output -------------------------------------------------------
        glm::vec3 EyePosition() const { return pose_.eye; }
        glm::mat4 ViewMatrix() const;
        glm::vec3 Forward() const;
        glm::vec3 Right() const;
        float AerialDistance() const { return aerialDistance_; }
        glm::vec2 AerialPivot() const { return aerialPivot_; }

    private:
        void TickWalk(float deltaSeconds, const FCameraWorld& world);
        void TickAerial(float deltaSeconds, const FCameraWorld& world);
        void TickFocus(float deltaSeconds, const FCameraWorld& world);
        // Elevation the orbit has to climb to for the subject to be visible.
        float ResolveOrbitPitch(float distance, const FCameraWorld& world) const;
        // Highest roof around the subject, measured once per subject.
        void MeasureSubjectSkyline(float distance, const FCameraWorld& world);
        void BeginBlend(float duration);

        EViewMode mode_ = EViewMode::Walk;
        EWalkCamera walkCamera_ = EWalkCamera::Follow;

        // Shared heading. Walk and Aerial keep the same yaw so switching
        // between them does not spin the city around.
        float yaw_ = 0.0f;
        float pitch_ = Config::kSpawnPitch;

        // Walk
        glm::vec3 smoothedWalker_{0.0f};
        bool walkerTracked_ = false;
        float followDistance_ = Config::kFollowDistance;
        float resolvedFollow_ = Config::kFollowDistance;
        glm::vec3 freePosition_{0.0f, 60.0f, 0.0f};

        // Aerial
        glm::vec2 aerialPivot_{0.0f};
        float aerialDistance_ = Config::kAerialDistance;
        float aerialPitch_ = Config::kAerialPitch;

        // Focus
        FFocusSubject subject_;
        float orbitYaw_ = 0.0f;
        float focusPitch_ = Config::kFocusPitch;
        float resolvedFocusPitch_ = Config::kFocusPitch;
        float focusZoom_ = 1.0f;
        float orbitSpeed_ = Config::kFocusOrbitSpeed;
        bool autoOrbit_ = true;
        float orbitResume_ = 0.0f;
        float subjectSkyline_ = 0.0f;
        bool skylineDirty_ = true;

        FCameraMoveInput move_;
        bool lookActive_ = false;

        FCameraPose pose_;
        FCameraPose desired_;
        FCameraPose blendFrom_;
        float blendRemaining_ = 0.0f;
        float blendDuration_ = 0.0f;
    };
}
