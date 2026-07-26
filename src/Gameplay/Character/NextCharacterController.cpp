#include "Gameplay/Character/NextCharacterController.h"

#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

NextCharacterController::NextCharacterController() = default;
NextCharacterController::~NextCharacterController() = default;

void NextCharacterController::Create(NextPhysics* physics, const FCharacterControllerSettings& settings)
{
    settings_ = settings;
    backend_ = physics ? physics->CreateCharacterController(settings) : nullptr;
}

void NextCharacterController::Destroy()
{
    backend_.reset();
}

void NextCharacterController::Update(const glm::vec3& inputDirection, float speed, bool jump, float deltaSeconds)
{
    if (backend_)
    {
        backend_->Update(inputDirection, speed, jump, deltaSeconds);
    }
}

bool NextCharacterController::TrySetHeight(float height)
{
    if (!backend_ || !backend_->TrySetHeight(height))
    {
        return false;
    }
    settings_.height = backend_->GetHeight();
    return true;
}

void NextCharacterController::SetPosition(const glm::vec3& position)
{
    if (backend_)
    {
        backend_->SetPosition(position);
    }
}

glm::vec3 NextCharacterController::GetPosition() const
{
    return backend_ ? backend_->GetPosition() : glm::vec3(0.0f);
}

glm::vec3 NextCharacterController::GetLinearVelocity() const
{
    return backend_ ? backend_->GetLinearVelocity() : glm::vec3(0.0f);
}

ECharacterGroundState NextCharacterController::GetGroundState() const
{
    return backend_ ? backend_->GetGroundState() : ECharacterGroundState::InAir;
}

bool NextCharacterController::IsOnGround() const
{
    return GetGroundState() == ECharacterGroundState::OnGround;
}

float NextCharacterController::GetHeight() const
{
    return backend_ ? backend_->GetHeight() : settings_.height;
}

bool NextCharacterController::IsValid() const
{
    return backend_ && backend_->IsValid();
}
