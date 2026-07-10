#pragma once

#include <memory>
#include <glm/glm.hpp>
#include <glm/detail/type_quat.hpp>

#include "Engine/Runtime/Subsystems/NextPhysics.h"

class NextCharacterController final
{
public:
    NextCharacterController();
    ~NextCharacterController();

    /// Create the character. Must be called after NextPhysics::Start().
    void Create(NextPhysics* physics, const FCharacterControllerSettings& settings);

    /// Destroy the character virtual.
    void Destroy();

    /// Must be called every frame before physics tick.
    /// @param inputDirection  World-space horizontal movement direction (normalized or zero).
    /// @param speed           Movement speed (m/s).
    /// @param jump            True on the frame the player presses jump.
    /// @param deltaSeconds    Frame delta time.
    void Update(const glm::vec3& inputDirection, float speed, bool jump, float deltaSeconds);

    glm::vec3 GetPosition() const;
    glm::vec3 GetLinearVelocity() const;
    ECharacterGroundState GetGroundState() const;
    bool IsOnGround() const;
    float GetHeight() const { return settings_.height; }
    float GetRadius() const { return settings_.radius; }

    bool IsValid() const;

private:
    std::unique_ptr<INextCharacterControllerBackend> backend_;
    FCharacterControllerSettings settings_{};
};
