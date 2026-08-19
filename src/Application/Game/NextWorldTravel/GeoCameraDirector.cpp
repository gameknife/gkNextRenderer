#include "GeoCameraDirector.h"

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace NextWorldTravel
{
    namespace
    {
        constexpr float kPitchLimit = 1.45f; // just under 90 degrees

        glm::vec3 DirFromAngles(float yaw, float pitch)
        {
            return glm::normalize(glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                            -std::cos(pitch) * std::cos(yaw)));
        }

        // Yaw a camera sitting at `eye` while looking at `center` orbits on.
        float YawFromOffset(const glm::vec3& eyeMinusCenter)
        {
            return std::atan2(-eyeMinusCenter.x, eyeMinusCenter.z);
        }

        glm::vec3 FlattenDirection(const glm::vec3& direction, const glm::vec3& fallback)
        {
            const glm::vec3 flat(direction.x, 0.0f, direction.z);
            return glm::length(flat) > 0.001f ? glm::normalize(flat) : fallback;
        }

        float GroundAt(const FCameraWorld& world, float x, float z, float fallback)
        {
            return world.terrain != nullptr ? world.terrain->SampleHeight(x, z) : fallback;
        }

        // Height of whatever is on top at (x, z) — a roof if there is a
        // building there, the terrain otherwise. One ray straight down.
        float SkylineAt(const FCameraWorld& world, float x, float z)
        {
            const float ground = GroundAt(world, x, z, 0.0f);
            if (!world.probe)
            {
                return ground;
            }
            const float from = ground + Config::kSkylineProbeHeight;
            const float hit = world.probe(glm::vec3(x, from, z), glm::vec3(0.0f, -1.0f, 0.0f));
            return hit < 0.0f ? ground : from - hit;
        }
    }

    void FGeoCameraDirector::BeginBlend(float duration)
    {
        blendFrom_ = pose_;
        blendDuration_ = duration;
        blendRemaining_ = duration;
    }

    void FGeoCameraDirector::SetMode(EViewMode mode, const FCameraWorld& world)
    {
        if (mode == mode_)
        {
            return;
        }
        BeginBlend(Config::kModeBlendSeconds);
        if (mode == EViewMode::Aerial)
        {
            // Come up over whatever was being looked at, not over the tile
            // origin: losing your place is the whole failure mode of a map view.
            const glm::vec3 anchor = world.walkerValid ? world.walkerPosition : pose_.target;
            aerialPivot_ = glm::clamp(glm::vec2(anchor.x, anchor.z),
                                      glm::vec2(-Config::kAerialPivotRange),
                                      glm::vec2(Config::kAerialPivotRange));
        }
        if (mode == EViewMode::Walk)
        {
            // Coming back from a browse mode always lands behind the character;
            // resuming a free camera parked two blocks away reads as a bug.
            walkCamera_ = EWalkCamera::Follow;
            resolvedFollow_ = followDistance_;
        }
        mode_ = mode;
    }

    void FGeoCameraDirector::SetWalkCamera(EWalkCamera camera)
    {
        if (camera == walkCamera_)
        {
            return;
        }
        if (camera == EWalkCamera::Free)
        {
            freePosition_ = pose_.eye;
        }
        walkCamera_ = camera;
    }

    void FGeoCameraDirector::SetFocusSubject(const FFocusSubject& subject)
    {
        subject_ = subject;
        if (!subject_.valid)
        {
            return;
        }
        // Enter the orbit on the bearing the camera already has, so a focus
        // change is a move in, not a swing around.
        const glm::vec3 offset = pose_.eye - subject_.center;
        if (glm::length(glm::vec2(offset.x, offset.z)) > 0.5f)
        {
            orbitYaw_ = YawFromOffset(offset);
        }
        resolvedFocusPitch_ = focusPitch_;
        skylineDirty_ = true;
        BeginBlend(Config::kFocusBlendSeconds);
    }

    void FGeoCameraDirector::SetLookActive(bool active)
    {
        lookActive_ = active;
        if (!active)
        {
            // Dragging the orbit by hand and having it immediately drift again
            // fights the user; give the manual bearing a moment to stand.
            orbitResume_ = Config::kOrbitResumeDelay;
        }
    }

    void FGeoCameraDirector::AddLook(float dx, float dy)
    {
        switch (mode_)
        {
        case EViewMode::Aerial:
            yaw_ += dx;
            aerialPitch_ = std::clamp(aerialPitch_ - dy, Config::kAerialPitchMin,
                                      Config::kAerialPitchMax);
            break;
        case EViewMode::Focus:
            orbitYaw_ += dx;
            focusPitch_ = std::clamp(focusPitch_ - dy, Config::kFocusPitchMin,
                                     Config::kFocusPitchMax);
            break;
        case EViewMode::Walk:
        default:
            yaw_ += dx;
            pitch_ = std::clamp(pitch_ - dy, -kPitchLimit, kPitchLimit);
            break;
        }
    }

    void FGeoCameraDirector::AddZoom(float wheel)
    {
        switch (mode_)
        {
        case EViewMode::Aerial:
            // Multiplicative: one notch has to mean the same thing at 2 km up
            // and at 100 m up.
            aerialDistance_ = std::clamp(aerialDistance_ * std::exp(-wheel * Config::kAerialZoomRate),
                                         Config::kAerialMinDistance, Config::kAerialMaxDistance);
            break;
        case EViewMode::Focus:
            focusZoom_ = std::clamp(focusZoom_ * std::exp(-wheel * Config::kFocusZoomRate),
                                    Config::kFocusZoomMin, Config::kFocusZoomMax);
            break;
        case EViewMode::Walk:
        default:
            if (walkCamera_ == EWalkCamera::Follow)
            {
                followDistance_ = std::clamp(followDistance_ - wheel * 1.5f,
                                             Config::kFollowMinDistance, Config::kFollowMaxDistance);
            }
            break;
        }
    }

    void FGeoCameraDirector::LookAt(const glm::vec3& point)
    {
        const glm::vec3 from = (mode_ == EViewMode::Walk && walkCamera_ == EWalkCamera::Follow)
                                   ? smoothedWalker_
                                   : pose_.eye;
        const glm::vec3 delta = point - from;
        const float horizontal = glm::length(glm::vec2(delta.x, delta.z));
        if (horizontal < 0.01f && std::abs(delta.y) < 0.01f)
        {
            return;
        }
        yaw_ = std::atan2(delta.x, -delta.z);
        pitch_ = std::clamp(std::atan2(delta.y, horizontal), -kPitchLimit, kPitchLimit);
    }

    void FGeoCameraDirector::SetHeading(float yaw, float pitch)
    {
        yaw_ = yaw;
        pitch_ = std::clamp(pitch, -kPitchLimit, kPitchLimit);
    }

    void FGeoCameraDirector::TickWalk(float deltaSeconds, const FCameraWorld& world)
    {
        const glm::vec3 direction = DirFromAngles(yaw_, pitch_);
        if (walkCamera_ == EWalkCamera::Free)
        {
            const float speed = Config::kFreeFlySpeed * (move_.sprint ? 4.0f : 1.0f);
            // Flying is steered by where the camera is aimed, not by the blended
            // pose: during a mode transition the two differ.
            const glm::vec3 right = glm::normalize(glm::cross(direction, glm::vec3(0.0f, 1.0f, 0.0f)));
            glm::vec3 delta = direction * move_.forward + right * move_.right +
                              glm::vec3(0.0f, 1.0f, 0.0f) * move_.up;
            if (glm::length(delta) > 0.001f)
            {
                freePosition_ += glm::normalize(delta) * speed * deltaSeconds;
            }
            desired_.eye = freePosition_;
            desired_.target = freePosition_ + direction;
            return;
        }

        const glm::vec3 focus = smoothedWalker_ + glm::vec3(0.0f, Config::kFollowHeight, 0.0f);
        float allowed = followDistance_;
        if (world.probe)
        {
            // Cast from the character out along the boom: anything hit is
            // between the camera and the character. The origin is pushed clear
            // of the character's own rig first — it is ordinary scene geometry,
            // and a ray started at chest height hits its own back immediately,
            // collapsing the boom every frame.
            const glm::vec3 boom = -direction;
            const float hit = world.probe(focus + boom * Config::kCameraCollisionStart, boom);
            if (hit >= 0.0f)
            {
                const float blocked = Config::kCameraCollisionStart + hit;
                if (blocked < followDistance_)
                {
                    allowed = std::max(Config::kFollowMinDistance,
                                       blocked - Config::kCameraCollisionPadding);
                }
            }
        }
        // Snap in immediately when a wall appears (otherwise the camera spends
        // the blend inside it) and ease back out when it clears.
        const float blend = 1.0f - std::exp(-Config::kCameraSharpness * deltaSeconds);
        resolvedFollow_ = allowed < resolvedFollow_
                              ? allowed
                              : resolvedFollow_ + (allowed - resolvedFollow_) * blend;

        desired_.eye = focus - direction * resolvedFollow_;
        desired_.target = focus;
    }

    void FGeoCameraDirector::TickAerial(float deltaSeconds, const FCameraWorld& world)
    {
        const glm::vec3 direction = DirFromAngles(yaw_, aerialPitch_);
        const glm::vec3 flatForward = FlattenDirection(direction, glm::vec3(0.0f, 0.0f, -1.0f));
        const glm::vec3 flatRight = glm::normalize(glm::cross(flatForward, glm::vec3(0.0f, 1.0f, 0.0f)));

        const float panSpeed = Config::kAerialPanRate * aerialDistance_ * (move_.sprint ? 3.0f : 1.0f);
        const glm::vec3 pan = flatForward * move_.forward + flatRight * move_.right;
        if (glm::length(pan) > 0.001f)
        {
            const glm::vec3 step = glm::normalize(pan) * panSpeed * deltaSeconds;
            aerialPivot_ = glm::clamp(aerialPivot_ + glm::vec2(step.x, step.z),
                                      glm::vec2(-Config::kAerialPivotRange),
                                      glm::vec2(Config::kAerialPivotRange));
        }
        if (std::abs(move_.up) > 0.001f)
        {
            aerialDistance_ = std::clamp(
                aerialDistance_ * std::exp(move_.up * Config::kAerialKeyZoomRate * deltaSeconds),
                Config::kAerialMinDistance, Config::kAerialMaxDistance);
        }

        const glm::vec3 pivot(aerialPivot_.x, GroundAt(world, aerialPivot_.x, aerialPivot_.y, 0.0f),
                              aerialPivot_.y);
        glm::vec3 eye = pivot - direction * aerialDistance_;
        eye.y = std::max(eye.y, GroundAt(world, eye.x, eye.z, 0.0f) + Config::kAerialGroundClearance);
        desired_.eye = eye;
        desired_.target = pivot;
    }

    float FGeoCameraDirector::ResolveOrbitPitch(float distance, const FCameraWorld& world) const
    {
        if (!world.probe)
        {
            return focusPitch_;
        }
        // Probe towards the subject rather than away from it: the orbit centre
        // of a building sits inside the building, so a ray cast outwards from
        // there hits the subject's own wall every time. The subject's half
        // extent is what separates "I hit the thing I am looking at" from "I hit
        // whatever is standing in front of it".
        const float clearance = distance - subject_.halfExtent - Config::kFocusOcclusionPadding;
        float steepest = focusPitch_;
        for (int step = 0; step <= Config::kFocusOcclusionSteps; ++step)
        {
            const float candidate = std::clamp(focusPitch_ - static_cast<float>(step) * Config::kFocusOcclusionStep,
                                               Config::kFocusPitchMin, Config::kFocusPitchMax);
            steepest = candidate;
            const glm::vec3 eye = subject_.center - DirFromAngles(orbitYaw_, candidate) * distance;
            const glm::vec3 toCenter = glm::normalize(subject_.center - eye);
            const float hit = world.probe(eye, toCenter);
            if (hit < 0.0f || hit >= clearance)
            {
                return candidate;
            }
        }
        // Nothing cleared: the steepest angle is still the best view available.
        return steepest;
    }

    void FGeoCameraDirector::MeasureSubjectSkyline(float distance, const FCameraWorld& world)
    {
        if (!skylineDirty_ || !world.probe || !subject_.valid)
        {
            return;
        }
        // Sampled once per subject rather than per frame: the roofs do not move
        // while the camera orbits, and a per-frame measurement turns into a
        // feedback loop between the elevation and what it can see.
        const float radius = distance * Config::kSkylineSampleFraction;
        float top = SkylineAt(world, subject_.center.x, subject_.center.z);
        for (int bearing = 0; bearing < Config::kSkylineBearings; ++bearing)
        {
            const float angle = 6.2831853f * static_cast<float>(bearing) /
                                static_cast<float>(Config::kSkylineBearings);
            top = std::max(top, SkylineAt(world, subject_.center.x + std::cos(angle) * radius,
                                          subject_.center.z + std::sin(angle) * radius));
        }
        subjectSkyline_ = top;
        skylineDirty_ = false;
    }

    void FGeoCameraDirector::TickFocus(float deltaSeconds, const FCameraWorld& world)
    {
        if (!subject_.valid)
        {
            return; // hold the last pose rather than snapping to the origin
        }
        if (orbitResume_ > 0.0f)
        {
            orbitResume_ = std::max(0.0f, orbitResume_ - deltaSeconds);
        }
        if (autoOrbit_ && !lookActive_ && orbitResume_ <= 0.0f)
        {
            orbitYaw_ += orbitSpeed_ * deltaSeconds;
        }

        float distance = std::max(4.0f, subject_.radius * focusZoom_);

        // A clear line to the subject is not the same as a view of it. Framed
        // from its own height, a 55 m station in a district of 300 m towers is
        // seen down a canyon: the sight line happens to be open and everything
        // around it is wall. So the orbit is lifted over the roofs standing
        // around the subject.
        MeasureSubjectSkyline(distance, world);
        const float required = subjectSkyline_ + Config::kFocusSkylineClearance - subject_.center.y;
        if (required > distance * Config::kFocusMaxLiftSine)
        {
            // The lift the neighbours demand is steeper than this distance can
            // give without ending up straight overhead. Backing off is the way
            // out: the subject gets smaller, but it is seen from an angle
            // instead of as a roof.
            distance = std::min(required / Config::kFocusMaxLiftSine, Config::kFocusLiftedMaxRadius);
        }

        float wanted = ResolveOrbitPitch(distance, world);
        if (required > 0.0f)
        {
            wanted = std::clamp(std::min(wanted, -std::asin(std::min(required / distance, 0.99f))),
                                Config::kFocusPitchMin, Config::kFocusPitchMax);
        }

        const float blend = 1.0f - std::exp(-Config::kFocusPitchSharpness * deltaSeconds);
        resolvedFocusPitch_ += (wanted - resolvedFocusPitch_) * blend;

        glm::vec3 eye = subject_.center - DirFromAngles(orbitYaw_, resolvedFocusPitch_) * distance;
        eye.y = std::max(eye.y, GroundAt(world, eye.x, eye.z, 0.0f) + Config::kFocusGroundClearance);
        desired_.eye = eye;
        desired_.target = subject_.center;
    }

    void FGeoCameraDirector::Tick(float deltaSeconds, const FCameraWorld& world)
    {
        const float dt = deltaSeconds > 0.0f ? deltaSeconds : 1.0f / 60.0f;

        // The walker is tracked in every mode: coming back to Walk from a tour
        // three blocks away must not start with a snap.
        if (world.walkerValid)
        {
            if (!walkerTracked_)
            {
                smoothedWalker_ = world.walkerPosition;
                walkerTracked_ = true;
            }
            else
            {
                const float blend = 1.0f - std::exp(-Config::kCameraSharpness * dt);
                smoothedWalker_ += (world.walkerPosition - smoothedWalker_) * blend;
            }
        }

        switch (mode_)
        {
        case EViewMode::Aerial: TickAerial(dt, world); break;
        case EViewMode::Focus: TickFocus(dt, world); break;
        case EViewMode::Walk:
        default: TickWalk(dt, world); break;
        }

        if (blendRemaining_ > 0.0f && blendDuration_ > 0.0f)
        {
            blendRemaining_ = std::max(0.0f, blendRemaining_ - dt);
            const float t = 1.0f - blendRemaining_ / blendDuration_;
            const float smooth = t * t * (3.0f - 2.0f * t);
            pose_.eye = glm::mix(blendFrom_.eye, desired_.eye, smooth);
            pose_.target = glm::mix(blendFrom_.target, desired_.target, smooth);
        }
        else
        {
            pose_ = desired_;
        }
    }

    glm::vec3 FGeoCameraDirector::Forward() const
    {
        const glm::vec3 delta = pose_.target - pose_.eye;
        return glm::length(delta) > 0.0001f ? glm::normalize(delta) : glm::vec3(0.0f, 0.0f, -1.0f);
    }

    glm::vec3 FGeoCameraDirector::Right() const
    {
        const glm::vec3 forward = Forward();
        const glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
        return glm::length(right) > 0.0001f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
    }

    glm::mat4 FGeoCameraDirector::ViewMatrix() const
    {
        // A degenerate pose (before the first tick, or a subject the camera sits
        // exactly on) would make lookAt produce NaNs and blank the frame.
        const glm::vec3 forward = Forward();
        return glm::lookAt(pose_.eye, pose_.eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
    }
}
