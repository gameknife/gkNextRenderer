#include "Gameplay/Character/NextCharacterController.h"

#include "Engine/Runtime/Subsystems/NextPhysics.hpp"

NextCharacterController::NextCharacterController() = default;
NextCharacterController::~NextCharacterController() = default;

void NextCharacterController::Create(NextPhysics* physics, const FCharacterControllerSettings& settings)
{
    physics_ = physics;
    settings_ = settings;
    backend_ = physics ? physics->CreateCharacterController(settings) : nullptr;
}

void NextCharacterController::Destroy()
{
    CompletePhysics();
    backend_.reset();
    physics_ = nullptr;
}

void NextCharacterController::Update(const glm::vec3& inputDirection, float speed, bool jump, float deltaSeconds)
{
    CompletePhysics();
    if (backend_)
    {
        backend_->Update(inputDirection, speed, jump, deltaSeconds);
    }
}

void NextCharacterController::UpdateWithVelocity(const glm::vec3& worldVelocity, bool inheritGround,
                                                 float deltaSeconds)
{
    CompletePhysics();
    if (backend_)
    {
        backend_->UpdateWithVelocity(worldVelocity, inheritGround, deltaSeconds);
    }
}

glm::vec3 NextCharacterController::GetGroundVelocity() const
{
    CompletePhysics();
    return backend_ ? backend_->GetGroundVelocity() : glm::vec3(0.0f);
}

glm::vec3 NextCharacterController::GetGroundNormal() const
{
    CompletePhysics();
    return backend_ ? backend_->GetGroundNormal() : glm::vec3(0.0f, 1.0f, 0.0f);
}

NextBodyID NextCharacterController::GetGroundBodyID() const
{
    CompletePhysics();
    return backend_ ? backend_->GetGroundBodyID() : NextBodyID();
}

bool NextCharacterController::TrySetHeight(float height)
{
    CompletePhysics();
    if (!backend_ || !backend_->TrySetHeight(height))
    {
        return false;
    }
    settings_.height = backend_->GetHeight();
    return true;
}

void NextCharacterController::SetPosition(const glm::vec3& position)
{
    CompletePhysics();
    if (backend_)
    {
        backend_->SetPosition(position);
    }
}

glm::vec3 NextCharacterController::GetPosition() const
{
    CompletePhysics();
    return backend_ ? backend_->GetPosition() : glm::vec3(0.0f);
}

glm::vec3 NextCharacterController::GetLinearVelocity() const
{
    CompletePhysics();
    return backend_ ? backend_->GetLinearVelocity() : glm::vec3(0.0f);
}

ECharacterGroundState NextCharacterController::GetGroundState() const
{
    CompletePhysics();
    return backend_ ? backend_->GetGroundState() : ECharacterGroundState::InAir;
}

bool NextCharacterController::IsOnGround() const
{
    return GetGroundState() == ECharacterGroundState::OnGround;
}

float NextCharacterController::GetHeight() const
{
    CompletePhysics();
    return backend_ ? backend_->GetHeight() : settings_.height;
}

bool NextCharacterController::IsValid() const
{
    CompletePhysics();
    return backend_ && backend_->IsValid();
}

void NextCharacterController::CompletePhysics() const
{
    if (physics_)
    {
        physics_->CompleteTick();
    }
}
