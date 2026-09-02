#include "Application/Game/NextAstrobot/Player/FollowCamera.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "Engine/Assets/Acceleration/CPUAccelerationStructure.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"

#include "Application/Game/NextAstrobot/Mechanisms/MechanismCurves.hpp"

namespace NextAstrobot
{
    namespace
    {
        // Shortest signed angular difference, so the auto-yaw never takes the long way round.
        float WrapAngle(float radians)
        {
            while (radians > kPi) radians -= 2.0f * kPi;
            while (radians < -kPi) radians += 2.0f * kPi;
            return radians;
        }

        glm::vec3 BoomOffset(const FCameraConfig& config, float yaw)
        {
            // Engine space: the camera sits behind the yaw direction and above the target.
            return glm::vec3(-std::sin(yaw) * config.Distance, config.Height, -std::cos(yaw) * config.Distance);
        }

        /// Longest boom, as a fraction of the full one, that keeps the lens clear of the
        /// level. Everything the player is meant to see through - pickups, characters,
        /// decoration, the player's own rig - is already off the raycast list, so the only
        /// things that pull the camera in are the surfaces they would actually stand on.
        float ClearBoomFraction(Assets::Scene& scene, const glm::vec3& target, const glm::vec3& boom,
                                const FCameraConfig& config)
        {
            const float length = glm::length(boom);
            if (length < 1e-3f)
            {
                return 1.0f;
            }
            const Assets::RayCastResult hit =
                scene.GetCPUAccelerationStructure().RayCastInCPU(target, boom / length);
            if (!hit.Hit || hit.T >= length - config.SpringArmRadius)
            {
                return 1.0f;
            }
            // Stop a sphere's radius short of whatever we hit, with no lower bound. A
            // minimum boom length sounds safer but is the opposite: back the player into a
            // pillar closer than the minimum and it puts the lens through the wall, which
            // renders as a black screen. Sitting on the character's shoulder is the
            // recoverable failure, so the obstacle always wins and the rig gets hidden.
            // A hard floor well inside the near plane: at zero the lens would sit exactly
            // on the look-at point and glm::lookAt would hand back a degenerate matrix.
            constexpr float kMinBoom = 0.4f;
            const float clear = std::max(hit.T - config.SpringArmRadius, kMinBoom);
            return std::clamp(clear / length, 0.0f, 1.0f);
        }
    }

    void FLevelCameras::Clear()
    {
        hasTitle_ = false;
        introNode_ = nullptr;
        introDuration_ = 0.0f;
    }

    void FLevelCameras::Bind(Assets::Scene& scene, const std::string& titleName, const std::string& introPathName)
    {
        Clear();
        // The game scrubs the fly-through itself so the shot lines up with the flow's
        // intro state; leaving the engine to ping-pong the track would fight that.
        scene.SetTracksPlaying(false);
        for (const Assets::Camera& camera : scene.GetEnvSettings().cameras)
        {
            if (!hasTitle_ && camera.name == titleName)
            {
                hasTitle_ = true;
                titleModelView_ = camera.ModelView;
                titleFov_ = camera.FieldOfView;
            }
            else if (!introNode_ && camera.name == introPathName)
            {
                // The path camera's node is driven by an AnimationTrack the loader built
                // from the gk_camera_lookat_key markers.
                introNode_ = scene.GetNode(camera.NodeName_);
                introFov_ = camera.FieldOfView;
            }
        }
        if (!introNode_)
        {
            return;
        }
        for (const Assets::AnimationTrack& track : scene.Tracks())
        {
            if (track.NodeName_ == introNode_->GetName())
            {
                introDuration_ = std::max(introDuration_, track.Duration_);
            }
        }
        if (introDuration_ <= 0.0f)
        {
            introNode_ = nullptr;
        }
    }

    void FLevelCameras::FillTitle(Assets::Camera& outCamera) const
    {
        outCamera.ModelView = titleModelView_;
        outCamera.FieldOfView = titleFov_;
        outCamera.NearPlane = 0.1f;
        outCamera.FarPlane = 1200.0f;
    }

    void FLevelCameras::AdvanceIntro(Assets::Scene& scene, float elapsed) const
    {
        if (introNode_)
        {
            scene.EvaluateTracks(std::clamp(elapsed, 0.0f, introDuration_));
        }
    }

    void FLevelCameras::FillIntro(Assets::Camera& outCamera) const
    {
        if (!introNode_)
        {
            return;
        }
        // The path node is a rigid transform, so its inverse is the view matrix.
        outCamera.ModelView = glm::inverse(introNode_->WorldTransform());
        outCamera.FieldOfView = introFov_;
        outCamera.NearPlane = 0.1f;
        outCamera.FarPlane = 1200.0f;
    }

    glm::vec3 FFollowCamera::Forward() const
    {
        return glm::vec3(std::sin(yaw_), 0.0f, std::cos(yaw_));
    }

    glm::vec3 FFollowCamera::Right() const
    {
        // glm::lookAt uses a right-handed view: when looking along +Z, the
        // screen's right points toward -X. Keep the movement basis aligned
        // with that view, so A and D always mean screen-left and screen-right.
        return glm::vec3(-std::cos(yaw_), 0.0f, std::sin(yaw_));
    }

    void FFollowCamera::Snap(const glm::vec3& footPosition, float yaw)
    {
        yaw_ = yaw;
        boomScale_ = 1.0f;
        target_ = footPosition + glm::vec3(0.0f, config_.TargetHeight, 0.0f);
        position_ = target_ + BoomOffset(config_, yaw_);
        manualIdleSeconds_ = config_.AutoYawIdleSeconds;
    }

    void FFollowCamera::AddManualYaw(float amount)
    {
        yaw_ += amount;
        yaw_ = WrapAngle(yaw_);
        manualIdleSeconds_ = 0.0f;
    }

    void FFollowCamera::Update(const glm::vec3& footPosition, const glm::vec3& horizontalVelocity,
                               float deltaSeconds, Assets::Scene* scene)
    {
        manualIdleSeconds_ += deltaSeconds;

        // Auto-yaw: once the player has stopped steering the camera, drift it behind the
        // direction of travel so a chase does not need constant stick work.
        const float speedSq = horizontalVelocity.x * horizontalVelocity.x + horizontalVelocity.z * horizontalVelocity.z;
        if (manualIdleSeconds_ >= config_.AutoYawIdleSeconds && speedSq > 1.0f)
        {
            const float desired = std::atan2(horizontalVelocity.x, horizontalVelocity.z);
            const float delta = WrapAngle(desired - yaw_);
            const float step = config_.AutoYawRate * deltaSeconds;
            yaw_ = WrapAngle(yaw_ + std::clamp(delta, -step, step));
        }

        const glm::vec3 desiredTarget = footPosition + glm::vec3(0.0f, config_.TargetHeight, 0.0f);
        const glm::vec3 boom = BoomOffset(config_, yaw_);

        // Spring arm. Pulling in is instant - a frame of seeing through a pillar is worse
        // than a hard cut - while extending back out eases, so brushing past a lamp post
        // does not fling the camera backwards.
        const float clearScale = scene ? ClearBoomFraction(*scene, desiredTarget, boom, config_) : 1.0f;
        if (clearScale < boomScale_)
        {
            boomScale_ = clearScale;
        }
        else
        {
            const float boomLength = std::max(glm::length(boom), 0.01f);
            const float step = config_.SpringArmReturnRate * deltaSeconds / boomLength;
            boomScale_ = std::min(clearScale, boomScale_ + step);
        }

        const glm::vec3 desiredPosition = desiredTarget + boom;
        target_.x = Damp(target_.x, desiredTarget.x, config_.Damping, deltaSeconds);
        target_.y = Damp(target_.y, desiredTarget.y, config_.Damping, deltaSeconds);
        target_.z = Damp(target_.z, desiredTarget.z, config_.Damping, deltaSeconds);
        position_.x = Damp(position_.x, desiredPosition.x, config_.Damping, deltaSeconds);
        position_.y = Damp(position_.y, desiredPosition.y, config_.Damping, deltaSeconds);
        position_.z = Damp(position_.z, desiredPosition.z, config_.Damping, deltaSeconds);

        // The damping runs against the full-length boom and the arm is applied after it,
        // as a hard clamp on the distance: damping toward an already-shortened boom would
        // let the lens drift through the wall for the frames the filter takes to catch up.
        // It has to be a clamp and not a scale - scaling an already-damped offset every
        // frame compounds, and the boom collapses onto the look-at point until glm::lookAt
        // hands back a degenerate matrix and the screen goes black.
        const float allowed = glm::length(boom) * boomScale_;
        const glm::vec3 offset = position_ - target_;
        const float distance = glm::length(offset);
        if (distance > allowed && distance > 1e-4f)
        {
            position_ = target_ + offset * (allowed / distance);
        }
    }

    void FFollowCamera::Fill(Assets::Camera& outCamera) const
    {
        outCamera.ModelView = glm::lookAt(position_, target_, glm::vec3(0.0f, 1.0f, 0.0f));
        outCamera.FieldOfView = config_.Fov;
        outCamera.NearPlane = 0.1f;
        outCamera.FarPlane = 1200.0f;
    }
}
