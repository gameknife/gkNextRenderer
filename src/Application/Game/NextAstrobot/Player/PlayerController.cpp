#include "Application/Game/NextAstrobot/Player/PlayerController.hpp"

#include <algorithm>
#include <cmath>

#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

#include "Application/Game/NextAstrobot/Mechanisms/MechanismCurves.hpp"

namespace NextAstrobot
{
    const char* LocomotionName(ELocomotion state)
    {
        switch (state)
        {
        case ELocomotion::Idle: return "idle";
        case ELocomotion::Run: return "run";
        case ELocomotion::Skid: return "skid";
        case ELocomotion::Jump: return "jump";
        case ELocomotion::Fall: return "fall";
        case ELocomotion::Hover: return "hover";
        case ELocomotion::Zip: return "zip";
        case ELocomotion::Punch: return "punch";
        case ELocomotion::Dead: return "dead";
        }
        return "unknown";
    }

    float FPlayerController::PunchRange() const
    {
        return punchStage_ >= 3 ? config_.KickRange : config_.PunchRange;
    }

    float FPlayerController::PunchArcDegrees() const
    {
        return punchStage_ >= 3 ? config_.KickArcDegrees : config_.PunchArcDegrees;
    }

    void FPlayerController::Create(NextPhysics* physics, const glm::vec3& spawnFootPosition, float yaw)
    {
        FCharacterControllerSettings settings;
        settings.height = config_.ControllerHeight;
        settings.radius = config_.ControllerRadius;
        settings.maxStepHeight = config_.MaxStepHeight;
        settings.maxSlopeAngle = config_.MaxSlopeDegrees;
        settings.initialPosition = spawnFootPosition;
        controller_.Create(physics, settings);

        position_ = spawnFootPosition;
        velocity_ = glm::vec3(0.0f);
        surfaceVelocity_ = glm::vec3(0.0f);
        wind_ = glm::vec3(0.0f);
        lift_ = 0.0f;
        yaw_ = yaw;
        onGround_ = false;
        state_ = ELocomotion::Fall;
        deathReason_.clear();
        coyoteTimer_ = 0.0f;
        jumpBufferTimer_ = 0.0f;
        hoverRemaining_ = config_.HoverMaxSeconds;
        hoverBoostTimer_ = 0.0f;
        hoverUsed_ = false;
        hoverActive_ = false;
        jumpReleasedInAir_ = false;
        jumpRising_ = false;
        punchTimer_ = 0.0f;
        punchStarted_ = false;
        punchStage_ = 0;
        comboTimer_ = 0.0f;
        punchBuffer_ = 0.0f;
        lunge_ = 0.0f;
        skidTimer_ = 0.0f;
    }

    void FPlayerController::Destroy()
    {
        controller_.Destroy();
    }

    float FPlayerController::HorizontalSpeed() const
    {
        return std::sqrt(velocity_.x * velocity_.x + velocity_.z * velocity_.z);
    }

    glm::vec3 FPlayerController::Facing() const
    {
        return glm::vec3(std::sin(yaw_), 0.0f, std::cos(yaw_));
    }

    void FPlayerController::Launch(float height)
    {
        if (state_ == ELocomotion::Dead)
        {
            return;
        }
        // v = sqrt(2*g*h): the launch pads are authored as "throws you this many metres up".
        Bounce(std::sqrt(2.0f * config_.Gravity * std::max(height, 0.0f)));
    }

    void FPlayerController::Bounce(float speed)
    {
        if (state_ == ELocomotion::Dead)
        {
            return;
        }
        velocity_.y = speed;
        onGround_ = false;
        // Not a player jump: a pad or a stomp is a fixed launch, so releasing the jump key
        // must not halve it the way the variable-height jump cut does.
        jumpRising_ = false;
        coyoteTimer_ = 0.0f;
        hoverUsed_ = false;
        hoverActive_ = false;
        hoverBoostTimer_ = 0.0f;
        jumpReleasedInAir_ = true;
        hoverRemaining_ = config_.HoverMaxSeconds;
        state_ = ELocomotion::Jump;
    }

    void FPlayerController::Kill(const std::string& reason)
    {
        if (godMode_ || state_ == ELocomotion::Dead)
        {
            return;
        }
        deathReason_ = reason;
        state_ = ELocomotion::Dead;
        velocity_ = glm::vec3(0.0f);
    }

    void FPlayerController::Respawn(const glm::vec3& footPosition, float yaw)
    {
        controller_.SetPosition(footPosition);
        position_ = footPosition;
        velocity_ = glm::vec3(0.0f);
        surfaceVelocity_ = glm::vec3(0.0f);
        wind_ = glm::vec3(0.0f);
        lift_ = 0.0f;
        yaw_ = yaw;
        state_ = ELocomotion::Fall;
        deathReason_.clear();
        hoverUsed_ = false;
        hoverActive_ = false;
        hoverBoostTimer_ = 0.0f;
        jumpReleasedInAir_ = false;
        hoverRemaining_ = config_.HoverMaxSeconds;
        punchTimer_ = 0.0f;
        punchStage_ = 0;
        comboTimer_ = 0.0f;
        punchBuffer_ = 0.0f;
        lunge_ = 0.0f;
        skidTimer_ = 0.0f;
        jumpRising_ = false;
    }

    void FPlayerController::Teleport(const glm::vec3& footPosition)
    {
        controller_.SetPosition(footPosition);
        position_ = footPosition;
        velocity_ = glm::vec3(0.0f);
        if (state_ != ELocomotion::Dead)
        {
            state_ = ELocomotion::Fall;
        }
    }

    void FPlayerController::BeginZip(const glm::vec3& from, const glm::vec3& to, float speed)
    {
        if (state_ == ELocomotion::Dead)
        {
            return;
        }
        zipFrom_ = from;
        zipTo_ = to;
        zipSpeed_ = std::max(speed, 0.5f);
        zipDistance_ = glm::length(to - from);
        zipTravelled_ = 0.0f;
        velocity_ = glm::vec3(0.0f);
        state_ = ELocomotion::Zip;
        controller_.SetPosition(from);
        position_ = from;
    }

    void FPlayerController::UpdateZip(float deltaSeconds)
    {
        if (zipDistance_ <= 0.001f)
        {
            state_ = ELocomotion::Fall;
            return;
        }
        zipTravelled_ = std::min(zipTravelled_ + zipSpeed_ * deltaSeconds, zipDistance_);
        const glm::vec3 direction = (zipTo_ - zipFrom_) / zipDistance_;
        const glm::vec3 next = zipFrom_ + direction * zipTravelled_;
        // Zip is scripted traversal: position is authoritative, contacts only get refreshed.
        controller_.SetPosition(next);
        position_ = next;
        velocity_ = direction * zipSpeed_;
        yaw_ = std::atan2(direction.x, direction.z);
        if (zipTravelled_ >= zipDistance_ - 0.001f)
        {
            state_ = ELocomotion::Fall;
            velocity_ = direction * zipSpeed_ * 0.5f;
            velocity_.y = 0.0f;
        }
    }

    void FPlayerController::Update(const FPlayerInput& input, const glm::vec3& cameraForward,
                                   const glm::vec3& cameraRight, float deltaSeconds)
    {
        punchStarted_ = false;
        const glm::vec3 inheritedSurface = surfaceVelocity_;
        const glm::vec3 draught = wind_;
        const float lift = lift_;
        surfaceVelocity_ = glm::vec3(0.0f);
        wind_ = glm::vec3(0.0f);
        lift_ = 0.0f;

        if (!controller_.IsValid() || deltaSeconds <= 0.0f)
        {
            return;
        }

        if (state_ == ELocomotion::Dead)
        {
            // Fall through the world while the death fade runs; no input, no hazards.
            velocity_.y -= config_.Gravity * deltaSeconds;
            controller_.UpdateWithVelocity(glm::vec3(0.0f, velocity_.y, 0.0f), false, deltaSeconds);
            position_ = controller_.GetPosition();
            return;
        }

        if (state_ == ELocomotion::Zip)
        {
            if (input.jumpPressed)
            {
                // Bail out of the ride: keep the momentum and give a small hop.
                state_ = ELocomotion::Jump;
                velocity_.y = config_.JumpSpeed * 0.6f;
                jumpReleasedInAir_ = false;
                hoverActive_ = false;
            }
            else
            {
                UpdateZip(deltaSeconds);
                return;
            }
        }

        // --- three-hit combo: left jab -> right cross -> spin kick ---
        // A press thrown mid-swing is buffered rather than dropped: without that the
        // player has to wait for the recovery frames to end and the chain never lands.
        punchBuffer_ = input.punchPressed ? config_.ComboBufferSeconds
                                          : std::max(0.0f, punchBuffer_ - deltaSeconds);
        punchTimer_ = std::max(0.0f, punchTimer_ - deltaSeconds);
        comboTimer_ = std::max(0.0f, comboTimer_ - deltaSeconds);
        if (punchBuffer_ > 0.0f && punchTimer_ <= 0.0f)
        {
            if (!onGround_)
            {
                // In the air, an attack immediately performs the spin kick (stage 3).
                punchStage_ = 3;
                punchTimer_ = config_.KickSeconds;
                comboTimer_ = 0.0f;
                punchBuffer_ = 0.0f;
                punchStarted_ = true;
                lunge_ = config_.KickLungeSpeed;
                skidTimer_ = 0.0f;
                if (hoverActive_)
                {
                    hoverActive_ = false;
                    hoverUsed_ = true;
                    hoverBoostTimer_ = 0.0f;
                }
            }
            else
            {
                // Ground combo: left jab -> right cross -> spin kick.
                punchStage_ = (comboTimer_ > 0.0f && punchStage_ >= 1 && punchStage_ < 3) ? punchStage_ + 1 : 1;
                punchTimer_ = punchStage_ == 3   ? config_.KickSeconds
                              : punchStage_ == 2 ? config_.PunchSeconds2
                                                 : config_.PunchSeconds;
                comboTimer_ = punchTimer_ + config_.ComboWindowSeconds;
                punchBuffer_ = 0.0f;
                punchStarted_ = true;
                lunge_ = punchStage_ == 3 ? config_.KickLungeSpeed : config_.PunchLungeSpeed;
                // Attacking cancels a skid: the brake pose and the swing fight over the same bones.
                skidTimer_ = 0.0f;
            }
        }
        if (punchTimer_ <= 0.0f && comboTimer_ <= 0.0f)
        {
            punchStage_ = 0;
        }
        lunge_ = punchTimer_ > 0.0f ? Damp(lunge_, 0.0f, 9.0f, deltaSeconds) : 0.0f;

        // --- horizontal ---
        const glm::vec3 forward = glm::normalize(glm::vec3(cameraForward.x, 0.0f, cameraForward.z));
        const glm::vec3 right = glm::normalize(glm::vec3(cameraRight.x, 0.0f, cameraRight.z));
        glm::vec3 wish = forward * input.move.y + right * input.move.x;
        const float wishLength = glm::length(wish);
        if (wishLength > 1.0f)
        {
            wish /= wishLength;
        }

        // --- skid stop: a hard reversal at speed brakes before it turns ---
        // Left to the run accel alone a full reversal takes about a quarter second of
        // the character sliding backwards with a forward-leaning run cycle on it, which
        // reads as an animation glitch rather than as a turn.
        if (skidTimer_ > 0.0f)
        {
            skidTimer_ = onGround_ ? std::max(0.0f, skidTimer_ - deltaSeconds) : 0.0f;
        }
        else if (onGround_ && punchTimer_ <= 0.0f && wishLength > 0.5f)
        {
            const glm::vec3 travel(velocity_.x, 0.0f, velocity_.z);
            const float travelSpeed = glm::length(travel);
            if (travelSpeed >= config_.SkidMinSpeed &&
                glm::dot(travel / travelSpeed, wish / wishLength) <= config_.SkidReverseDot)
            {
                skidTimer_ = config_.SkidSeconds;
            }
        }
        const bool skidding = skidTimer_ > 0.0f;

        const float control = onGround_ ? 1.0f : config_.AirControl;
        // Steering happens relative to the air: in a draught the target velocity is the
        // player's own run added to whatever the fan is doing to the air around them.
        // A fan is a jump aid, so it barely tugs at someone standing: at full strength on
        // the ground it shoves the player off the small discs it is supposed to help them
        // reach, and a platformer that slides you off a ledge you are standing still on is
        // just annoying.
        constexpr float kGroundWindScale = 0.25f;
        const glm::vec3 stream = draught * (onGround_ ? kGroundWindScale : 1.0f);
        // While skidding the target is a standstill, so the brake is the only thing acting
        // on the velocity; while attacking the stage carries the character forward and the
        // stick only trims it, which is what makes a combo close the distance.
        glm::vec3 run = wish * config_.RunSpeed;
        if (skidding)
        {
            run = glm::vec3(0.0f);
        }
        else if (punchTimer_ > 0.0f)
        {
            run = wish * config_.RunSpeed * 0.25f + Facing() * lunge_;
        }
        const glm::vec3 desired = run + glm::vec3(stream.x, 0.0f, stream.z);
        const float accel = skidding ? config_.SkidDecel : config_.RunAccel * control;
        velocity_.x = Approach(velocity_.x, desired.x, accel, deltaSeconds);
        velocity_.z = Approach(velocity_.z, desired.z, accel, deltaSeconds);

        if (wishLength > 0.05f)
        {
            // Face travel direction; the rig reads yaw_ directly.
            const float targetYaw = std::atan2(wish.x, wish.z);
            float delta = targetYaw - yaw_;
            while (delta > kPi) delta -= 2.0f * kPi;
            while (delta < -kPi) delta += 2.0f * kPi;
            const float turnScale = skidding                ? config_.SkidTurnScale
                                    : punchTimer_ > 0.0f    ? 0.35f
                                                            : 1.0f;
            const float step = glm::radians(config_.TurnRateDegrees) * turnScale * deltaSeconds;
            yaw_ += std::clamp(delta, -step, step);
        }

        // --- vertical: coyote time, jump buffer, variable height, hover ---
        coyoteTimer_ = onGround_ ? config_.CoyoteSeconds : std::max(0.0f, coyoteTimer_ - deltaSeconds);
        jumpBufferTimer_ = input.jumpPressed ? config_.JumpBufferSeconds
                                             : std::max(0.0f, jumpBufferTimer_ - deltaSeconds);

        if (jumpBufferTimer_ > 0.0f && coyoteTimer_ > 0.0f)
        {
            velocity_.y = config_.JumpSpeed;
            jumpRising_ = true;
            jumpBufferTimer_ = 0.0f;
            coyoteTimer_ = 0.0f;
            onGround_ = false;
            jumpReleasedInAir_ = false;
            hoverActive_ = false;
        }
        else if (onGround_ && velocity_.y <= 0.0f)
        {
            // A small downward bias keeps ExtendedUpdate's stick-to-floor in contact with
            // slopes and with platforms that are descending under us.
            velocity_.y = -1.0f;
            jumpRising_ = false;
            hoverUsed_ = false;
            hoverActive_ = false;
            jumpReleasedInAir_ = false;
            hoverRemaining_ = config_.HoverMaxSeconds;
        }
        else if (!onGround_ && punchTimer_ > 0.0f && punchStage_ == 3)
        {
            // Air spin kick: reduce vertical descent so the kick sweeps horizontally across the air
            if (velocity_.y < -1.8f)
            {
                velocity_.y = Approach(velocity_.y, -1.8f, config_.Gravity * 1.5f, deltaSeconds);
            }
            else
            {
                velocity_.y -= config_.Gravity * 0.4f * deltaSeconds;
            }
        }
        else
        {
            velocity_.y -= config_.Gravity * deltaSeconds;
        }

        if (jumpRising_ && !input.jumpHeld && velocity_.y > 0.0f)
        {
            velocity_.y *= config_.JumpCutMultiplier;
            jumpRising_ = false;
        }
        if (velocity_.y <= 0.0f)
        {
            jumpRising_ = false;
        }

        if (!onGround_ && !input.jumpHeld)
        {
            jumpReleasedInAir_ = true;
        }

        // Hover does not transition automatically from a held jump. Instead, the player must
        // release the jump key after takeoff, then press and hold it again in mid-air.
        // On activation it gives an ascending boost climbing HoverBoostHeight across HoverBoostSeconds
        // (default: 1.6m in 1.0s), then eases into the downward hover glide.
        if (!onGround_ && !hoverUsed_ && hoverRemaining_ > 0.0f && jumpReleasedInAir_ && !hoverActive_ &&
            input.jumpPressed)
        {
            hoverActive_ = true;
            jumpRising_ = false;
            hoverBoostTimer_ = config_.HoverBoostSeconds;
        }

        if (hoverActive_)
        {
            if (input.jumpHeld && hoverRemaining_ > 0.0f && !onGround_)
            {
                hoverRemaining_ -= deltaSeconds;
                if (hoverBoostTimer_ > 0.0f)
                {
                    hoverBoostTimer_ -= deltaSeconds;
                    const float boostFrac =
                        std::clamp(1.0f - (hoverBoostTimer_ / std::max(config_.HoverBoostSeconds, 0.05f)), 0.0f, 1.0f);
                    const float vAvg = config_.HoverBoostHeight / std::max(config_.HoverBoostSeconds, 0.05f);
                    // Linear ease from 1.5*vAvg to 0.5*vAvg over HoverBoostSeconds:
                    // the integral over [0, T] is exactly vAvg * T = HoverBoostHeight (e.g. 1.6m in 1.0s).
                    const float vStart = vAvg * 1.5f;
                    const float vEnd = vAvg * 0.5f;
                    velocity_.y = glm::mix(vStart, vEnd, boostFrac);
                }
                else
                {
                    if (velocity_.y > config_.HoverFallSpeed)
                    {
                        velocity_.y =
                            Approach(velocity_.y, config_.HoverFallSpeed, config_.Gravity * 0.8f, deltaSeconds);
                    }
                    else
                    {
                        velocity_.y = config_.HoverFallSpeed;
                    }
                }
                if (hoverRemaining_ <= 0.0f)
                {
                    hoverActive_ = false;
                    hoverUsed_ = true;
                    hoverBoostTimer_ = 0.0f;
                }
            }
            else
            {
                // Releasing the button ends this hover for good; it recharges on landing.
                hoverActive_ = false;
                hoverUsed_ = true;
                hoverBoostTimer_ = 0.0f;
            }
        }

        // A fountain carries. It lands after the jump arc so it reads as the world acting
        // on the player rather than as a different jump. The fan is already folded into
        // the horizontal target above: a draught changes where "standing still" is.
        if (lift > 0.0f)
        {
            velocity_.y = std::max(velocity_.y, lift);
            // Clearing the ground flag stops the stick-to-floor bias from swallowing the
            // first frame of the ride.
            onGround_ = false;
            jumpRising_ = false;
        }

        const glm::vec3 stepVelocity = velocity_ + glm::vec3(inheritedSurface.x, 0.0f, inheritedSurface.z);
        controller_.UpdateWithVelocity(stepVelocity, true, deltaSeconds);

        const glm::vec3 previous = position_;
        position_ = controller_.GetPosition();
        const bool wasOnGround = onGround_;
        onGround_ = controller_.IsOnGround();
        if (onGround_ && !wasOnGround)
        {
            hoverUsed_ = false;
            hoverActive_ = false;
            hoverBoostTimer_ = 0.0f;
            jumpReleasedInAir_ = false;
            hoverRemaining_ = config_.HoverMaxSeconds;
        }
        // A ceiling or a wall can eat the requested motion; keep the internal velocity in
        // step with what actually happened so the fall curve does not run away.
        if (deltaSeconds > 0.0f && velocity_.y > 0.0f && position_.y - previous.y < 0.001f)
        {
            velocity_.y = 0.0f;
            jumpRising_ = false;
            hoverBoostTimer_ = 0.0f;
        }

        if (punchTimer_ > 0.0f)
        {
            state_ = ELocomotion::Punch;
        }
        else if (onGround_)
        {
            state_ = skidding                  ? ELocomotion::Skid
                     : HorizontalSpeed() > 0.4f ? ELocomotion::Run
                                                : ELocomotion::Idle;
        }
        else if (hoverActive_)
        {
            state_ = ELocomotion::Hover;
        }
        else
        {
            state_ = velocity_.y > 0.0f ? ELocomotion::Jump : ELocomotion::Fall;
        }
    }
}
