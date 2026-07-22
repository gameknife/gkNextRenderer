#pragma once

// ============================================================================
// PlayerController.hpp - The "3C" core: a Jolt-backed NextCharacterController
// plus first/third-person camera (yaw/pitch), WASD/sprint/jump movement and
// ADS-driven FOV smoothing. Owns all player-movement state; the GameInstance
// only feeds it input and reads back the camera basis.
// ============================================================================

#include <glm/glm.hpp>

#include "Gameplay/Character/NextCharacterController.h"

#include "Application/Game/NextDayz/NextDayzConfig.hpp"

class NextPhysics;

namespace NextDayz
{
    enum class EMoveState
    {
        Idle,
        Walk,
        Run
    };

    class PlayerController
    {
    public:
        void Create(NextPhysics* physics, const glm::vec3& spawnPosition, const FConfig& config);
        void Destroy();
        bool IsValid() const { return controller_.IsValid(); }

        // --- input feed (called from GameInstance input handlers) ---
        void OnLook(float dxPixels, float dyPixels);
        void SetMoveKey(int dir, bool pressed); // 0=fwd 1=back 2=left 3=right
        void SetSprinting(bool sprinting) { sprinting_ = sprinting; }
        void QueueJump() { jumpQueued_ = true; }
        void SetAiming(bool aiming) { aiming_ = aiming; }
        void SetAimFov(float fov) { aimFov_ = fov; } // absolute FOV target while ADS
        void ToggleView();
        void AdjustCameraDistance(float scrollY);

        // --- per-frame update ---
        void Update(float deltaSeconds);

        // --- queries ---
        glm::vec3 Position() const { return controller_.GetPosition(); }
        glm::vec3 EyePosition() const;
        glm::vec3 Forward() const;   // full 3D view direction (yaw+pitch)
        glm::vec3 Right() const;     // horizontal right
        glm::vec3 Up() const;
        float Yaw() const { return yaw_; }
        float CurrentFov() const { return currentFov_; }
        bool IsFirstPerson() const { return firstPerson_; }
        bool IsAiming() const { return aiming_; }
        bool IsSprinting() const { return sprinting_; }
        EMoveState MoveState() const { return moveState_; }
        float HorizontalSpeed() const;

        // Fills a render camera (view matrix + FOV) for FPS or TPS.
        void FillCamera(glm::mat4& outModelView, float& outFov) const;

    private:
        glm::vec3 MoveForward() const; // horizontal (yaw only)

        NextCharacterController controller_;
        FConfig config_{};

        float yaw_ = 0.0f;
        float pitch_ = 0.0f;
        bool firstPerson_ = true;
        bool sprinting_ = false;
        bool aiming_ = false;
        bool jumpQueued_ = false;
        bool keyMove_[4] = {false, false, false, false};

        float currentFov_ = 75.0f;
        float aimFov_ = 55.0f;
        float camDistance_ = 4.5f;

        EMoveState moveState_ = EMoveState::Idle;
    };
}
