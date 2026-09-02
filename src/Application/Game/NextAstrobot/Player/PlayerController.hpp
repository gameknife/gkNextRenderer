#pragma once

// ============================================================================
// PlayerController.hpp - The Astro-style 3C: run, jump with coyote time and a
// jump buffer, hover on a held jump, punch, stomp, launch pads, zipline rides,
// death and respawn. The game integrates the whole velocity itself and hands it
// to NextCharacterController::UpdateWithVelocity, which is what lets the
// character keep its footing on a moving platform.
// ============================================================================

#include <algorithm>
#include <string>

#include <glm/glm.hpp>

#include "Gameplay/Character/NextCharacterController.h"

#include "Application/Game/NextAstrobot/NextAstrobotConfig.hpp"

class NextPhysics;

namespace NextAstrobot
{
    enum class ELocomotion : uint8_t
    {
        Idle,
        Run,
        /// Braking through a hard reversal: the feet slide while the body swings round.
        Skid,
        Jump,
        Fall,
        Hover,
        Zip,
        Punch,
        Dead,
    };

    const char* LocomotionName(ELocomotion state);

    struct FPlayerInput
    {
        glm::vec2 move{0.0f};   // camera-relative, x = right, y = forward, already clamped to the unit disc
        bool jumpHeld = false;
        bool jumpPressed = false;
        bool punchPressed = false;
    };

    class FPlayerController
    {
    public:
        void Configure(const FMoveConfig& config) { config_ = config; }
        void Create(NextPhysics* physics, const glm::vec3& spawnFootPosition, float yaw);
        void Destroy();
        bool IsValid() const { return controller_.IsValid(); }

        void Update(const FPlayerInput& input, const glm::vec3& cameraForward, const glm::vec3& cameraRight,
                    float deltaSeconds);

        // --- world feedback, applied by the systems before the next Update ---
        /// Extra horizontal velocity from a conveyor / spinning disc / roller, in m/s.
        void AddSurfaceVelocity(const glm::vec3& velocity) { surfaceVelocity_ += velocity; }
        /// A fan draught, in m/s. The run controller steers against the moving air, so
        /// the player leans across a stream slower than their run speed and is carried by
        /// a faster one. Adding it as a force instead would vanish into the run damping.
        void AddWind(const glm::vec3& velocity) { wind_ += velocity; }
        /// A fountain column: while the player is inside it, they rise at least this fast.
        void ApplyLift(float speed) { lift_ = std::max(lift_, speed); }
        /// Launches the player to `height` metres above the current foot position.
        void Launch(float height);
        void Bounce(float speed);
        void Kill(const std::string& reason);
        void Respawn(const glm::vec3& footPosition, float yaw);
        void SetGodMode(bool enabled) { godMode_ = enabled; }
        bool GodMode() const { return godMode_; }
        void Teleport(const glm::vec3& footPosition);
        /// Turns the character on the spot. Used by the astro.ride debug cvar so a script
        /// that drops the player in front of a prop is also facing it.
        void SetYaw(float yaw) { yaw_ = yaw; }

        /// Attaches to a zipline: the controller stops simulating and is driven along the
        /// segment until it reaches the end (or the player jumps off).
        void BeginZip(const glm::vec3& from, const glm::vec3& to, float speed);
        bool IsZipping() const { return state_ == ELocomotion::Zip; }

        // --- queries ---
        glm::vec3 Position() const { return position_; }
        glm::vec3 Velocity() const { return velocity_; }
        glm::vec3 HorizontalVelocity() const { return glm::vec3(velocity_.x, 0.0f, velocity_.z); }
        float HorizontalSpeed() const;
        float Yaw() const { return yaw_; }
        bool IsOnGround() const { return onGround_; }
        ELocomotion State() const { return state_; }
        bool IsDead() const { return state_ == ELocomotion::Dead; }
        const std::string& DeathReason() const { return deathReason_; }
        /// True during the active frames of a punch; consumed by InteractableSystem/EnemySystem.
        bool PunchActive() const { return punchTimer_ > 0.0f; }
        /// True on the frame the punch started, so a hit resolves exactly once.
        bool PunchStarted() const { return punchStarted_; }
        /// 0 while not attacking, then 1 left jab, 2 right cross, 3 spin kick. The rig
        /// picks its clip from this and the hit test its reach.
        int PunchStage() const { return punchStage_; }
        /// Reach and arc of the stage being thrown; the kick sweeps the full circle.
        float PunchRange() const;
        float PunchArcDegrees() const;
        float ControllerHeight() const { return config_.ControllerHeight; }
        /// Foot-centred forward direction the punch cone is built around.
        glm::vec3 Facing() const;
        NextBodyID GroundBodyID() const { return controller_.GetGroundBodyID(); }

    private:
        void UpdateZip(float deltaSeconds);

        NextCharacterController controller_;
        FMoveConfig config_{};

        glm::vec3 position_{0.0f};
        glm::vec3 velocity_{0.0f};
        glm::vec3 surfaceVelocity_{0.0f};
        glm::vec3 wind_{0.0f};
        float lift_ = 0.0f;
        float yaw_ = 0.0f;
        bool onGround_ = false;
        bool godMode_ = false;

        ELocomotion state_ = ELocomotion::Idle;
        std::string deathReason_;

        float coyoteTimer_ = 0.0f;
        float jumpBufferTimer_ = 0.0f;
        float hoverRemaining_ = 0.0f;
        bool hoverUsed_ = false;
        bool jumpRising_ = false;
        float punchTimer_ = 0.0f;
        bool punchStarted_ = false;
        int punchStage_ = 0;
        /// Time left to chain into the next stage; outlives punchTimer_ by the combo window.
        float comboTimer_ = 0.0f;
        /// A press thrown during the previous stage, spent the moment that stage ends.
        float punchBuffer_ = 0.0f;
        /// Forward carry of the stage being thrown, in m/s, damped to zero.
        float lunge_ = 0.0f;
        float skidTimer_ = 0.0f;

        glm::vec3 zipFrom_{0.0f};
        glm::vec3 zipTo_{0.0f};
        float zipSpeed_ = 8.0f;
        float zipDistance_ = 0.0f;
        float zipTravelled_ = 0.0f;
    };
}
