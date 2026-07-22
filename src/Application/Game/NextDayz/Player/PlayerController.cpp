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

        FCharacterControllerSettings settings;
        settings.height = config_.Player.ControllerHeight;
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
        const float cosPitch = std::cos(pitch_);
        return glm::normalize(glm::vec3(std::sin(yaw_) * cosPitch, -std::sin(pitch_), std::cos(yaw_) * cosPitch));
    }

    glm::vec3 PlayerController::Up() const
    {
        return glm::normalize(glm::cross(Right(), Forward()));
    }

    glm::vec3 PlayerController::EyePosition() const
    {
        return controller_.GetPosition() + glm::vec3(0.0f, config_.Player.EyeHeight, 0.0f);
    }

    float PlayerController::HorizontalSpeed() const
    {
        const glm::vec3 v = controller_.GetLinearVelocity();
        return glm::length(glm::vec2(v.x, v.z));
    }

    void PlayerController::Update(float deltaSeconds)
    {
        // Sprint and ADS are mutually exclusive; sprinting cancels aiming.
        if (sprinting_)
        {
            aiming_ = false;
        }

        glm::vec3 moveDir(0.0f);
        const glm::vec3 fwd = MoveForward();
        const glm::vec3 right = Right();
        if (keyMove_[0]) moveDir += fwd;
        if (keyMove_[1]) moveDir -= fwd;
        if (keyMove_[3]) moveDir += right;
        if (keyMove_[2]) moveDir -= right;

        const bool moving = glm::length(moveDir) > 0.001f;
        if (moving)
        {
            moveDir = glm::normalize(moveDir);
        }

        float speed = sprinting_ ? config_.Player.RunSpeed : config_.Player.WalkSpeed;
        if (aiming_)
        {
            speed *= config_.Player.AimMoveScale;
        }

        controller_.Update(moveDir, moving ? speed : 0.0f, jumpQueued_, deltaSeconds);
        jumpQueued_ = false;

        // Classify locomotion for animation / HUD.
        const float hs = HorizontalSpeed();
        if (!moving || hs < 0.4f)
        {
            moveState_ = EMoveState::Idle;
        }
        else if (sprinting_ && hs > config_.Player.WalkSpeed * 0.9f)
        {
            moveState_ = EMoveState::Run;
        }
        else
        {
            moveState_ = EMoveState::Walk;
        }

        // Smoothly approach the target FOV (base or ADS).
        const float targetFov = aiming_ ? aimFov_ : config_.Camera.BaseFov;
        const float t = 1.0f - std::exp(-config_.Camera.FovLerpSpeed * std::max(0.0f, deltaSeconds));
        currentFov_ = glm::mix(currentFov_, targetFov, t);
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
            target = controller_.GetPosition() + glm::vec3(0.0f, config_.Camera.TpsHeight, 0.0f);
            eye = target - forward * camDistance_;
        }
        outModelView = glm::lookAt(eye, target, up);
        outFov = currentFov_;
    }
}
