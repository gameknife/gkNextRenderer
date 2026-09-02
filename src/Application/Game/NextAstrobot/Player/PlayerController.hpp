#pragma once

// ============================================================================
// PlayerController.hpp - The Astro-style 3C: run, jump with coyote time and a
// jump buffer, hover on a held jump, punch, stomp, launch pads, zipline rides,
// death and respawn. The game integrates the whole velocity itself and hands it
// to NextCharacterController::UpdateWithVelocity, which is what lets the
// character keep its footing on a moving platform.
// ============================================================================

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
        /// Launches the player to `height` metres above the current foot position.
        void Launch(float height);
        void Bounce(float speed);
        void Kill(const std::string& reason);
        void Respawn(const glm::vec3& footPosition, float yaw);
        void SetGodMode(bool enabled) { godMode_ = enabled; }
        bool GodMode() const { return godMode_; }
        void Teleport(const glm::vec3& footPosition);

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

        glm::vec3 zipFrom_{0.0f};
        glm::vec3 zipTo_{0.0f};
        float zipSpeed_ = 8.0f;
        float zipDistance_ = 0.0f;
        float zipTravelled_ = 0.0f;
    };
}
