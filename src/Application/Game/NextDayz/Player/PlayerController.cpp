#include "PlayerController.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include <glm/gtc/matrix_transform.hpp>

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

namespace NextDayz
{
    namespace
    {
        std::optional<Assets::RayCastResult> RaycastWithin(NextEngine& engine,
                                                           const glm::vec3& origin,
                                                           const glm::vec3& direction,
                                                           float maxDistance)
        {
            std::optional<Assets::RayCastResult> hit;
            engine.RayCast(origin, glm::normalize(direction), [&](Assets::RayCastResult result)
            {
                if (result.Hit && result.T >= 0.0f && result.T <= maxDistance)
                {
                    hit = result;
                }
                return true;
            });
            return hit;
        }

        float Smooth01(float value)
        {
            value = glm::clamp(value, 0.0f, 1.0f);
            return value * value * (3.0f - 2.0f * value);
        }
    }

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
        jumpPhaseElapsed_ = 0.0f;
        airborneHorizontalVelocity_ = glm::vec3(0.0f);
        traversalAction_ = EPlayerTraversalAction::None;
        traversalElapsed_ = 0.0f;
        traversalDuration_ = 0.0f;
        traversalTopY_ = 0.0f;
        traversalProbeResult_ = "not_tested";
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

    glm::vec3 PlayerController::CameraPosition() const
    {
        if (firstPerson_)
        {
            return EyePosition();
        }

        const glm::vec3 forward = Forward();
        constexpr glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 cameraRight = glm::normalize(glm::cross(forward, worldUp));
        const glm::vec3 characterFocus =
            controller_.GetPosition() + glm::vec3(0.0f, currentEyeHeight_ * 0.88f, 0.0f);
        return characterFocus - forward * camDistance_ +
               cameraRight * config_.Camera.TpsShoulderOffset;
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

    void PlayerController::QueueContextualJump(NextEngine& engine)
    {
        if (IsTraversing())
        {
            return;
        }
        if (!TryBeginTraversal(engine))
        {
            jumpQueued_ = true;
        }
    }

    bool PlayerController::TryBeginTraversal(NextEngine& engine)
    {
        traversalProbeResult_ = "checking";
        if (!locomotion_.onGround || movementLocked_ ||
            locomotion_.actualStance != EPlayerStance::Standing)
        {
            traversalProbeResult_ = !locomotion_.onGround
                                        ? "rejected:not_grounded"
                                    : movementLocked_
                                        ? "rejected:movement_locked"
                                        : "rejected:not_standing";
            return false;
        }

        const FTraversalConfig& traversal = config_.Traversal;
        const glm::vec3 feet = controller_.GetPosition();
        const glm::vec3 forward = MoveForward();
        const glm::vec3 right = Right();
        const auto frontHit = RaycastWithin(
            engine, feet + glm::vec3(0.0f, traversal.ProbeHeight, 0.0f),
            forward, traversal.ProbeDistance);
        if (!frontHit)
        {
            traversalProbeResult_ = "rejected:no_front_obstacle";
            return false;
        }

        const glm::vec3 frontNormal = glm::vec3(frontHit->Normal);
        if (frontNormal.y > 0.45f)
        {
            traversalProbeResult_ = "rejected:front_is_walkable";
            return false;
        }

        const float frontDistance = frontHit->T;
        const float downwardOriginY = feet.y + traversal.ClimbMaxHeight + 0.45f;
        const float downwardDistance = traversal.ClimbMaxHeight + 0.65f;
        const auto surfaceAt = [&](float distance, float lateral,
                                   std::optional<uint32_t> expectedInstance = std::nullopt)
            -> std::optional<Assets::RayCastResult>
        {
            const glm::vec3 origin =
                feet + forward * distance + right * lateral;
            auto hit = RaycastWithin(
                engine, glm::vec3(origin.x, downwardOriginY, origin.z),
                glm::vec3(0.0f, -1.0f, 0.0f), downwardDistance);
            if (!hit || glm::vec3(hit->Normal).y < traversal.SurfaceNormalMinY ||
                (expectedInstance && hit->InstanceId != *expectedInstance))
            {
                return std::nullopt;
            }
            return hit;
        };

        const auto topHit = surfaceAt(
            frontDistance + traversal.TopProbeInset, 0.0f, frontHit->InstanceId);
        if (!topHit)
        {
            traversalProbeResult_ = "rejected:no_matching_top_surface";
            return false;
        }

        const float obstacleTopY = static_cast<float>(topHit->HitPoint.y);
        const float obstacleHeight = obstacleTopY - feet.y;
        if (obstacleHeight < traversal.MinObstacleHeight ||
            obstacleHeight > traversal.ClimbMaxHeight)
        {
            traversalProbeResult_ = "rejected:height_out_of_range";
            return false;
        }

        const auto hasStandingClearance = [&](const glm::vec3& targetFeet)
        {
            const float clearanceDistance =
                config_.Player.StandingHeight + traversal.StandingClearance;
            for (float lateral : {0.0f, -config_.Player.ControllerRadius * 0.65f,
                                 config_.Player.ControllerRadius * 0.65f})
            {
                const glm::vec3 origin =
                    targetFeet + right * lateral + glm::vec3(0.0f, 0.04f, 0.0f);
                if (RaycastWithin(engine, origin, glm::vec3(0.0f, 1.0f, 0.0f),
                                  clearanceDistance))
                {
                    return false;
                }
            }
            return true;
        };

        EPlayerTraversalAction selectedAction = EPlayerTraversalAction::None;
        glm::vec3 targetFeet(0.0f);

        if (obstacleHeight <= traversal.VaultMaxHeight)
        {
            const float firstBehindDistance =
                frontDistance + config_.Player.ControllerRadius * 2.0f + 0.10f;
            const float lastBehindDistance =
                frontDistance + traversal.MaxVaultDepth +
                config_.Player.ControllerRadius + 0.15f;
            for (float distance = firstBehindDistance;
                 distance <= lastBehindDistance; distance += 0.15f)
            {
                const auto groundHit = surfaceAt(distance, 0.0f);
                if (!groundHit)
                {
                    continue;
                }
                const float groundY = static_cast<float>(groundHit->HitPoint.y);
                if (groundY <= feet.y + config_.Player.MaxStepHeight)
                {
                    targetFeet = glm::vec3(groundHit->HitPoint);
                    if (hasStandingClearance(targetFeet))
                    {
                        selectedAction = EPlayerTraversalAction::Vault;
                        break;
                    }
                }
            }
        }

        if (selectedAction == EPlayerTraversalAction::None)
        {
            const float standDistance =
                frontDistance + config_.Player.ControllerRadius +
                traversal.PlatformStandDepth;
            const auto center = surfaceAt(standDistance, 0.0f, frontHit->InstanceId);
            const auto left = surfaceAt(
                standDistance, -config_.Player.ControllerRadius * 0.65f,
                frontHit->InstanceId);
            const auto rightSurface = surfaceAt(
                standDistance, config_.Player.ControllerRadius * 0.65f,
                frontHit->InstanceId);
            if (center && left && rightSurface)
            {
                const float centerY = static_cast<float>(center->HitPoint.y);
                const float leftY = static_cast<float>(left->HitPoint.y);
                const float rightY = static_cast<float>(rightSurface->HitPoint.y);
                if (std::abs(centerY - obstacleTopY) <= 0.12f &&
                    std::abs(leftY - obstacleTopY) <= 0.12f &&
                    std::abs(rightY - obstacleTopY) <= 0.12f)
                {
                    targetFeet = glm::vec3(center->HitPoint);
                    if (hasStandingClearance(targetFeet))
                    {
                        selectedAction = EPlayerTraversalAction::ClimbUp;
                    }
                }
            }
        }

        if (selectedAction == EPlayerTraversalAction::None)
        {
            traversalProbeResult_ =
                obstacleHeight <= traversal.VaultMaxHeight
                    ? "rejected:no_vault_landing_or_clearance"
                    : "rejected:no_climb_stand_area_or_clearance";
            return false;
        }

        traversalAction_ = selectedAction;
        traversalElapsed_ = 0.0f;
        traversalDuration_ =
            selectedAction == EPlayerTraversalAction::Vault
                ? traversal.VaultDurationSeconds
                : traversal.ClimbDurationSeconds;
        traversalTopY_ = obstacleTopY;
        traversalStart_ = feet;
        traversalEnd_ = targetFeet;
        traversalProbeResult_ =
            selectedAction == EPlayerTraversalAction::Vault
                ? "accepted:vault"
                : "accepted:climb_up";
        jumpQueued_ = false;
        crouchToggleQueued_ = false;
        aiming_ = false;
        jumpPhaseElapsed_ = 0.0f;
        locomotion_.jumpPhase = EPlayerJumpPhase::None;
        locomotion_.jumpPhaseTime01 = 0.0f;
        locomotion_.traversalAction = traversalAction_;
        locomotion_.traversalTime01 = 0.0f;
        locomotion_.traversalHeight = obstacleHeight;
        return true;
    }

    void PlayerController::UpdateTraversal(float deltaSeconds)
    {
        const glm::vec3 previousPosition = controller_.GetPosition();
        traversalElapsed_ += std::max(deltaSeconds, 0.0f);
        const float time01 =
            glm::clamp(traversalElapsed_ / std::max(traversalDuration_, 0.01f),
                       0.0f, 1.0f);
        const float riseEnd =
            traversalAction_ == EPlayerTraversalAction::Vault ? 0.42f : 0.58f;
        const float riseForwardFraction =
            traversalAction_ == EPlayerTraversalAction::Vault ? 0.48f : 0.18f;
        const float apexY =
            std::max({traversalStart_.y, traversalEnd_.y, traversalTopY_}) +
            config_.Traversal.ArcClearance;
        const glm::vec3 riseEndPosition =
            glm::mix(traversalStart_, traversalEnd_, riseForwardFraction);

        glm::vec3 position;
        if (time01 < riseEnd)
        {
            const float phase = Smooth01(time01 / riseEnd);
            position = glm::mix(traversalStart_, riseEndPosition, phase);
            position.y = glm::mix(traversalStart_.y, apexY, phase);
        }
        else
        {
            const float phase = Smooth01((time01 - riseEnd) / (1.0f - riseEnd));
            position = glm::mix(
                glm::vec3(riseEndPosition.x, apexY, riseEndPosition.z),
                traversalEnd_, phase);
        }
        controller_.SetPosition(position);

        locomotion_.localMove = glm::vec2(0.0f);
        locomotion_.gait = EPlayerGait::Idle;
        locomotion_.worldVelocity =
            deltaSeconds > 0.0f ? (position - previousPosition) / deltaSeconds
                                : glm::vec3(0.0f);
        locomotion_.horizontalSpeed =
            glm::length(glm::vec2(locomotion_.worldVelocity.x,
                                  locomotion_.worldVelocity.z));
        locomotion_.onGround = time01 >= 1.0f;
        locomotion_.jumpPhase = EPlayerJumpPhase::None;
        locomotion_.jumpPhaseTime01 = 0.0f;
        locomotion_.traversalAction = traversalAction_;
        locomotion_.traversalTime01 = time01;

        if (time01 >= 1.0f)
        {
            traversalAction_ = EPlayerTraversalAction::None;
            traversalElapsed_ = 0.0f;
            airborneHorizontalVelocity_ = glm::vec3(0.0f);
        }
    }

    void PlayerController::UpdateView(float deltaSeconds)
    {
        const float eyeTarget = locomotion_.actualStance == EPlayerStance::Crouched
                                    ? config_.Player.CrouchedEyeHeight
                                    : config_.Player.StandingEyeHeight;
        const float eyeT = 1.0f - std::exp(-config_.Player.EyeHeightLerpSpeed *
                                           std::max(0.0f, deltaSeconds));
        currentEyeHeight_ = glm::mix(currentEyeHeight_, eyeTarget, eyeT);

        const float targetFov = aiming_ ? aimFov_ : config_.Camera.BaseFov;
        const float t = 1.0f - std::exp(-config_.Camera.FovLerpSpeed *
                                        std::max(0.0f, deltaSeconds));
        currentFov_ = glm::mix(currentFov_, targetFov, t);

        const float recoilDt = std::max(0.0f, deltaSeconds);
        cameraRecoilVelocity_ +=
            (-config_.Weapon.CameraRecoilSpring * cameraRecoil_ -
             config_.Weapon.CameraRecoilDamping * cameraRecoilVelocity_) *
            recoilDt;
        cameraRecoil_ += cameraRecoilVelocity_ * recoilDt;
    }

    void PlayerController::Update(float deltaSeconds)
    {
        if (IsTraversing())
        {
            UpdateTraversal(deltaSeconds);
            UpdateView(deltaSeconds);
            return;
        }
        locomotion_.traversalAction = EPlayerTraversalAction::None;
        locomotion_.traversalTime01 = 0.0f;
        locomotion_.traversalHeight = 0.0f;

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

        const bool wasOnGround = locomotion_.onGround;
        const bool jump = jumpQueued_ && !movementLocked_ && wasOnGround &&
                          locomotion_.actualStance == EPlayerStance::Standing;
        if (jump)
        {
            airborneHorizontalVelocity_ = moveDir * speed;
        }

        glm::vec3 physicsMoveDir = moveDir;
        float physicsSpeed = speed;
        if (!wasOnGround)
        {
            physicsSpeed = glm::length(glm::vec2(airborneHorizontalVelocity_.x,
                                                 airborneHorizontalVelocity_.z));
            physicsMoveDir = physicsSpeed > 0.001f
                                 ? airborneHorizontalVelocity_ / physicsSpeed
                                 : glm::vec3(0.0f);
        }

        controller_.Update(physicsMoveDir, physicsSpeed, jump, deltaSeconds);
        jumpQueued_ = false;

        locomotion_.worldVelocity = controller_.GetLinearVelocity();
        locomotion_.horizontalSpeed =
            glm::length(glm::vec2(locomotion_.worldVelocity.x, locomotion_.worldVelocity.z));
        locomotion_.onGround = controller_.IsOnGround();
        if (wasOnGround && !locomotion_.onGround && !jump)
        {
            // Walking off an edge keeps the velocity from the final grounded
            // physics step, just like an explicit jump.
            airborneHorizontalVelocity_ =
                glm::vec3(locomotion_.worldVelocity.x, 0.0f, locomotion_.worldVelocity.z);
        }
        else if (locomotion_.onGround)
        {
            airborneHorizontalVelocity_ = glm::vec3(0.0f);
        }

        const float jumpDt = std::max(deltaSeconds, 0.0f);
        const auto beginJumpPhase = [this](EPlayerJumpPhase phase)
        {
            locomotion_.jumpPhase = phase;
            locomotion_.jumpPhaseTime01 = 0.0f;
            jumpPhaseElapsed_ = 0.0f;
        };
        if (jump)
        {
            beginJumpPhase(EPlayerJumpPhase::Up);
        }
        else
        {
            switch (locomotion_.jumpPhase)
            {
            case EPlayerJumpPhase::None:
                if (!locomotion_.onGround)
                {
                    beginJumpPhase(locomotion_.worldVelocity.y > 0.15f
                                       ? EPlayerJumpPhase::Up
                                       : EPlayerJumpPhase::AirLoop);
                }
                break;
            case EPlayerJumpPhase::Up:
                jumpPhaseElapsed_ += jumpDt;
                locomotion_.jumpPhaseTime01 =
                    glm::clamp(jumpPhaseElapsed_ / std::max(config_.Animation.JumpUpSeconds, 0.01f),
                               0.0f, 1.0f);
                if (locomotion_.onGround && !wasOnGround)
                {
                    beginJumpPhase(EPlayerJumpPhase::Down);
                }
                else if (jumpPhaseElapsed_ >= config_.Animation.JumpUpSeconds ||
                         locomotion_.worldVelocity.y <= 0.0f)
                {
                    beginJumpPhase(EPlayerJumpPhase::AirLoop);
                }
                break;
            case EPlayerJumpPhase::AirLoop:
                if (locomotion_.onGround)
                {
                    beginJumpPhase(EPlayerJumpPhase::Down);
                }
                break;
            case EPlayerJumpPhase::Down:
                if (!locomotion_.onGround)
                {
                    beginJumpPhase(EPlayerJumpPhase::AirLoop);
                }
                else
                {
                    jumpPhaseElapsed_ += jumpDt;
                    locomotion_.jumpPhaseTime01 =
                        glm::clamp(jumpPhaseElapsed_ / std::max(config_.Animation.JumpDownSeconds, 0.01f),
                                   0.0f, 1.0f);
                    if (jumpPhaseElapsed_ >= config_.Animation.JumpDownSeconds)
                    {
                        beginJumpPhase(EPlayerJumpPhase::None);
                    }
                }
                break;
            }
        }

        UpdateView(deltaSeconds);
    }

    void PlayerController::FillCamera(glm::mat4& outModelView, float& outFov) const
    {
        const glm::vec3 forward = Forward();
        constexpr glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        const glm::vec3 eye = CameraPosition();
        outModelView = glm::lookAt(eye, eye + forward, worldUp);
        outFov = currentFov_;
    }
}
