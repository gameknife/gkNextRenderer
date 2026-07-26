#include "PlayerController.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

namespace NextDayz
{
    void PlayerController::Create(NextPhysics* physics, const glm::vec3& spawnPosition, const FConfig& config)
    {
        config_ = config;
        currentFov_ = config_.Camera.BaseFov;
        aimFov_ = config_.Camera.BaseFov;
        camDistance_ = config_.Camera.TpsDistance;
        currentEyeHeight_ = config_.Player.StandingEyeHeight;
        cameraRecoil_ = glm::vec2(0.0f);
        cameraRecoilVelocity_ = glm::vec2(0.0f);
        locomotion_ = {};
        walkModifier_ = false;
        sprintModifier_ = false;
        movementLocked_ = false;
        jumpQueued_ = false;
        crouchToggleQueued_ = false;
        std::fill(std::begin(keyMove_), std::end(keyMove_), false);

        FCharacterControllerSettings settings;
        settings.height = config_.Player.StandingHeight;
        settings.radius = config_.Player.ControllerRadius;
        settings.mass = config_.Player.ControllerMass;
        settings.maxStrength = config_.Player.ControllerStrength;
        settings.maxStepHeight = config_.Player.MaxStepHeight;
        settings.maxSlopeAngle = config_.Player.MaxSlopeAngle;
        settings.initialPosition = spawnPosition;
        controller_.Create(physics, settings);
    }

    void PlayerController::Destroy()
    {
        controller_.Destroy();
    }

    void PlayerController::OnLook(float dxPixels, float dyPixels)
    {
        yaw_ -= dxPixels * config_.Camera.MouseSensitivity;
        pitch_ += dyPixels * config_.Camera.MouseSensitivity;
        const float maxPitch = glm::radians(config_.Camera.MaxPitchDegrees);
        pitch_ = glm::clamp(pitch_, -maxPitch, maxPitch);
    }

    void PlayerController::SetMoveKey(int dir, bool pressed)
    {
        if (dir >= 0 && dir < 4)
        {
            keyMove_[dir] = pressed;
        }
    }

    void PlayerController::ToggleView()
    {
        firstPerson_ = !firstPerson_;
    }

    void PlayerController::AdjustCameraDistance(float scrollY)
    {
        camDistance_ = glm::clamp(camDistance_ - scrollY * config_.Camera.ScrollStep,
                                  config_.Camera.TpsMinDistance, config_.Camera.TpsMaxDistance);
    }

    glm::vec3 PlayerController::MoveForward() const
    {
        return glm::vec3(std::sin(yaw_), 0.0f, std::cos(yaw_));
    }

    glm::vec3 PlayerController::Right() const
    {
        return glm::vec3(-std::cos(yaw_), 0.0f, std::sin(yaw_));
    }

    glm::vec3 PlayerController::Forward() const
    {
        const float viewYaw = yaw_ + cameraRecoil_.x;
        const float maxPitch = glm::radians(config_.Camera.MaxPitchDegrees);
        const float viewPitch = glm::clamp(pitch_ + cameraRecoil_.y, -maxPitch, maxPitch);
        const float cosPitch = std::cos(viewPitch);
        return glm::normalize(
            glm::vec3(std::sin(viewYaw) * cosPitch, -std::sin(viewPitch), std::cos(viewYaw) * cosPitch));
    }

    glm::vec3 PlayerController::Up() const
    {
        return glm::normalize(glm::cross(Right(), Forward()));
    }

    glm::vec3 PlayerController::EyePosition() const
    {
        return controller_.GetPosition() + glm::vec3(0.0f, currentEyeHeight_, 0.0f);
    }

    float PlayerController::HorizontalSpeed() const
    {
        const glm::vec3 v = controller_.GetLinearVelocity();
        return glm::length(glm::vec2(v.x, v.z));
    }

    void PlayerController::ApplyCameraRecoil(const glm::vec2& impulseRadians)
    {
        cameraRecoilVelocity_ += impulseRadians;
    }

    bool PlayerController::RecoilActive() const
    {
        return glm::dot(cameraRecoil_, cameraRecoil_) > 0.000001f ||
               glm::dot(cameraRecoilVelocity_, cameraRecoilVelocity_) > 0.0001f;
    }

    void PlayerController::Update(float deltaSeconds)
    {
        if (crouchToggleQueued_)
        {
            locomotion_.desiredStance = locomotion_.desiredStance == EPlayerStance::Standing
                                             ? EPlayerStance::Crouched
                                             : EPlayerStance::Standing;
            crouchToggleQueued_ = false;
        }
        if (jumpQueued_ && locomotion_.actualStance == EPlayerStance::Crouched)
        {
            locomotion_.desiredStance = EPlayerStance::Standing;
        }

        locomotion_.standBlocked = false;
        if (locomotion_.desiredStance != locomotion_.actualStance)
        {
            const bool wantsStanding = locomotion_.desiredStance == EPlayerStance::Standing;
            const float targetHeight =
                wantsStanding ? config_.Player.StandingHeight : config_.Player.CrouchedHeight;
            if (controller_.TrySetHeight(targetHeight))
            {
                locomotion_.actualStance = locomotion_.desiredStance;
            }
            else if (wantsStanding)
            {
                locomotion_.standBlocked = true;
            }
        }

        glm::vec2 localMove(0.0f);
        if (keyMove_[0]) localMove.y += 1.0f;
        if (keyMove_[1]) localMove.y -= 1.0f;
        if (keyMove_[3]) localMove.x += 1.0f;
        if (keyMove_[2]) localMove.x -= 1.0f;
        if (glm::length(localMove) > 1.0f)
        {
            localMove = glm::normalize(localMove);
        }
        if (movementLocked_)
        {
            localMove = glm::vec2(0.0f);
        }
        locomotion_.localMove = localMove;

        const bool moving = glm::length(localMove) > 0.001f;
        const bool wantsSprint = moving && sprintModifier_ &&
                                 locomotion_.actualStance == EPlayerStance::Standing;
        if (wantsSprint)
        {
            aiming_ = false;
        }

        if (!moving)
        {
            locomotion_.gait = EPlayerGait::Idle;
        }
        else if (locomotion_.actualStance == EPlayerStance::Crouched || aiming_ || walkModifier_)
        {
            locomotion_.gait = EPlayerGait::Walk;
        }
        else if (wantsSprint)
        {
            locomotion_.gait = EPlayerGait::Sprint;
        }
        else
        {
            locomotion_.gait = EPlayerGait::Run;
        }

        float speed = 0.0f;
        if (moving)
        {
            if (locomotion_.actualStance == EPlayerStance::Crouched)
            {
                speed = config_.Player.CrouchWalkSpeed;
            }
            else
            {
                switch (locomotion_.gait)
                {
                case EPlayerGait::Walk: speed = config_.Player.StandWalkSpeed; break;
                case EPlayerGait::Sprint: speed = config_.Player.StandSprintSpeed; break;
                case EPlayerGait::Run: speed = config_.Player.StandRunSpeed; break;
                case EPlayerGait::Idle:
                default: break;
                }
            }

            const float absX = std::abs(localMove.x);
            const float absY = std::abs(localMove.y);
            const float forwardScale = localMove.y < 0.0f ? config_.Player.BackwardScale : 1.0f;
            const float axisWeight = absX + absY;
            if (axisWeight > 0.0f)
            {
                speed *= (absX * config_.Player.StrafeScale + absY * forwardScale) / axisWeight;
            }
            if (aiming_)
            {
                speed *= config_.Player.AimMoveScale;
            }
        }

        const glm::vec3 fwd = MoveForward();
        const glm::vec3 right = Right();
        glm::vec3 moveDir = fwd * localMove.y + right * localMove.x;
        if (glm::length(moveDir) > 0.001f)
        {
            moveDir = glm::normalize(moveDir);
        }

        const bool jump = jumpQueued_ && !movementLocked_ &&
                          locomotion_.actualStance == EPlayerStance::Standing;
        controller_.Update(moveDir, speed, jump, deltaSeconds);
        jumpQueued_ = false;

        locomotion_.worldVelocity = controller_.GetLinearVelocity();
        locomotion_.horizontalSpeed =
            glm::length(glm::vec2(locomotion_.worldVelocity.x, locomotion_.worldVelocity.z));
        locomotion_.onGround = controller_.IsOnGround();

        const float eyeTarget = locomotion_.actualStance == EPlayerStance::Crouched
                                    ? config_.Player.CrouchedEyeHeight
                                    : config_.Player.StandingEyeHeight;
        const float eyeT = 1.0f - std::exp(-config_.Player.EyeHeightLerpSpeed *
                                           std::max(0.0f, deltaSeconds));
        currentEyeHeight_ = glm::mix(currentEyeHeight_, eyeTarget, eyeT);

        // Smoothly approach the target FOV (base or ADS).
        const float targetFov = aiming_ ? aimFov_ : config_.Camera.BaseFov;
        const float t = 1.0f - std::exp(-config_.Camera.FovLerpSpeed * std::max(0.0f, deltaSeconds));
        currentFov_ = glm::mix(currentFov_, targetFov, t);

        const float recoilDt = std::max(0.0f, deltaSeconds);
        cameraRecoilVelocity_ +=
            (-config_.Weapon.CameraRecoilSpring * cameraRecoil_ -
             config_.Weapon.CameraRecoilDamping * cameraRecoilVelocity_) *
            recoilDt;
        cameraRecoil_ += cameraRecoilVelocity_ * recoilDt;
    }

    void PlayerController::FillCamera(glm::mat4& outModelView, float& outFov) const
    {
        const glm::vec3 forward = Forward();
        const glm::vec3 right = Right();
        const glm::vec3 up = glm::normalize(glm::cross(right, forward));

        glm::vec3 eye;
        glm::vec3 target;
        if (firstPerson_)
        {
            eye = EyePosition();
            target = eye + forward;
        }
        else
        {
            target = controller_.GetPosition() + glm::vec3(0.0f, currentEyeHeight_, 0.0f);
            eye = target - forward * camDistance_;
        }
        outModelView = glm::lookAt(eye, target, up);
        outFov = currentFov_;
    }
}
