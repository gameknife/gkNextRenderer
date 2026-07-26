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
#include "Application/Game/NextDayz/Player/PlayerState.hpp"

class NextPhysics;

namespace NextDayz
{
    class PlayerController
    {
    public:
        void Create(NextPhysics* physics, const glm::vec3& spawnPosition, const FConfig& config);
        void Destroy();
        bool IsValid() const { return controller_.IsValid(); }

        // --- input feed (called from GameInstance input handlers) ---
        void OnLook(float dxPixels, float dyPixels);
        void SetMoveKey(int dir, bool pressed); // 0=fwd 1=back 2=left 3=right
        void SetWalkModifier(bool pressed) { walkModifier_ = pressed; }
        void SetSprintModifier(bool pressed) { sprintModifier_ = pressed; }
        void QueueJump() { jumpQueued_ = true; }
        void QueueCrouchToggle() { crouchToggleQueued_ = true; }
        void SetAiming(bool aiming) { aiming_ = aiming; }
        void SetMovementLocked(bool locked) { movementLocked_ = locked; }
        void SetAimFov(float fov) { aimFov_ = fov; } // absolute FOV target while ADS
        void ApplyCameraRecoil(const glm::vec2& impulseRadians);
        void ToggleView();
        void AdjustCameraDistance(float scrollY);

        // --- per-frame update ---
        void Update(float deltaSeconds);

        // --- queries ---
        glm::vec3 Position() const { return controller_.GetPosition(); }
        glm::vec3 EyePosition() const;
        glm::vec3 CameraPosition() const;
        glm::vec3 Forward() const;   // full 3D view direction (yaw+pitch)
        glm::vec3 Right() const;     // horizontal right
        glm::vec3 Up() const;
        float Yaw() const { return yaw_; }
        float Pitch() const { return pitch_; }
        float CurrentFov() const { return currentFov_; }
        bool IsFirstPerson() const { return firstPerson_; }
        bool IsAiming() const { return aiming_; }
        bool IsSprinting() const { return locomotion_.gait == EPlayerGait::Sprint; }
        const FPlayerLocomotionState& LocomotionState() const { return locomotion_; }
        float HorizontalSpeed() const;
        float ControllerHeight() const { return controller_.GetHeight(); }
        bool RecoilActive() const;
        glm::vec2 CameraRecoil() const { return cameraRecoil_; }

        // Fills a render camera (view matrix + FOV) for FPS or TPS.
        void FillCamera(glm::mat4& outModelView, float& outFov) const;

    private:
        glm::vec3 MoveForward() const; // horizontal (yaw only)

        NextCharacterController controller_;
        FConfig config_{};

        float yaw_ = 0.0f;
        float pitch_ = 0.0f;
        bool firstPerson_ = true;
        bool walkModifier_ = false;
        bool sprintModifier_ = false;
        bool aiming_ = false;
        bool movementLocked_ = false;
        bool jumpQueued_ = false;
        bool crouchToggleQueued_ = false;
        bool keyMove_[4] = {false, false, false, false};

        float currentFov_ = 75.0f;
        float aimFov_ = 55.0f;
        float camDistance_ = 4.5f;
        float currentEyeHeight_ = 1.65f;
        glm::vec2 cameraRecoil_{0.0f};
        glm::vec2 cameraRecoilVelocity_{0.0f};
        FPlayerLocomotionState locomotion_{};
        float jumpPhaseElapsed_ = 0.0f;
        glm::vec3 airborneHorizontalVelocity_{0.0f};
    };
}
