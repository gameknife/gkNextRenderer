#include "Runtime/Subsystems/NextCharacterController.h"
#include "Runtime/Subsystems/NextPhysics.h"

#if WITH_PHYSIC
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

JPH_SUPPRESS_WARNINGS

using namespace JPH;
using namespace JPH::literals;

// Forward-declare the context that holds the Jolt PhysicsSystem pointer.
// NextPhysics stores it in a pimpl; we access it via a friend-style accessor
// added to NextPhysics.
extern JPH::PhysicsSystem* GetJoltPhysicsSystem(NextPhysics* physics);
extern JPH::TempAllocator* GetJoltTempAllocator(NextPhysics* physics);

struct FNextCharacterControllerContext
{
    Ref<CharacterVirtual> character;
    CharacterVirtual::ExtendedUpdateSettings updateSettings;
};

#else

struct FNextCharacterControllerContext {};

#endif

NextCharacterController::NextCharacterController() = default;
NextCharacterController::~NextCharacterController() = default;

void NextCharacterController::Create(NextPhysics* physics, const FCharacterControllerSettings& settings)
{
#if WITH_PHYSIC
    physics_ = physics;
    settings_ = settings;
    velocity_ = glm::vec3(0.0f);

    JPH::PhysicsSystem* system = GetJoltPhysicsSystem(physics);
    if (!system)
    {
        return;
    }

    // Create a capsule standing upright: half-height along Y, with radius.
    // CharacterVirtual expects the shape centered at origin.
    // Capsule in Jolt is along Y by default when using RotatedTranslated wrapper.
    const float cylinderHalfHeight = (settings.height - 2.0f * settings.radius) * 0.5f;
    RefConst<Shape> capsule = new CapsuleShape(std::max(cylinderHalfHeight, 0.01f), settings.radius);

    // Shift the capsule so its feet are at y=0
    const float shapeOffsetY = settings.height * 0.5f;
    RefConst<Shape> shape = new RotatedTranslatedShape(
        Vec3(0, shapeOffsetY, 0), Quat::sIdentity(), capsule);

    CharacterVirtualSettings cvSettings;
    cvSettings.mShape = shape;
    cvSettings.mMaxSlopeAngle = DegreesToRadians(settings.maxSlopeAngle);
    cvSettings.mMaxStrength = settings.maxStrength;
    cvSettings.mCharacterPadding = settings.padding;
    cvSettings.mPenetrationRecoverySpeed = 1.0f;
    cvSettings.mPredictiveContactDistance = 0.1f;
    cvSettings.mSupportingVolume = Plane(Vec3::sAxisY(), -settings.radius);
    cvSettings.mMass = settings.mass;

    context_ = std::make_unique<FNextCharacterControllerContext>();
    context_->character = new CharacterVirtual(&cvSettings,
        RVec3(settings.initialPosition.x, settings.initialPosition.y, settings.initialPosition.z),
        Quat::sIdentity(), 0, system);

    // Configure extended update settings for stairs/slopes
    context_->updateSettings.mStickToFloorStepDown = Vec3(0, -settings.maxStepHeight, 0);
    context_->updateSettings.mWalkStairsStepUp = Vec3(0, settings.maxStepHeight, 0);
#else
    (void)physics;
    (void)settings;
#endif
}

void NextCharacterController::Destroy()
{
#if WITH_PHYSIC
    context_.reset();
    physics_ = nullptr;
#endif
}

void NextCharacterController::Update(const glm::vec3& inputDirection, float speed, bool jump, float deltaSeconds)
{
#if WITH_PHYSIC
    if (!context_ || !context_->character)
    {
        return;
    }

    JPH::PhysicsSystem* system = GetJoltPhysicsSystem(physics_);
    JPH::TempAllocator* tempAlloc = GetJoltTempAllocator(physics_);
    if (!system || !tempAlloc)
    {
        return;
    }

    constexpr float gravity = -15.0f;
    constexpr float jumpSpeed = 6.0f;

    CharacterVirtual* ch = context_->character;

    // Determine ground state
    bool onGround = ch->GetGroundState() == CharacterVirtual::EGroundState::OnGround;

    // Horizontal velocity from input
    Vec3 desiredVelocity(inputDirection.x * speed, 0.0f, inputDirection.z * speed);

    // Vertical velocity: gravity + jump
    float verticalVel = velocity_.y;
    if (onGround)
    {
        verticalVel = 0.0f;
        if (jump)
        {
            verticalVel = jumpSpeed;
        }
    }
    else
    {
        verticalVel += gravity * deltaSeconds;
    }

    Vec3 newVelocity(desiredVelocity.GetX(), verticalVel, desiredVelocity.GetZ());
    ch->SetLinearVelocity(newVelocity);

    // Store for GetLinearVelocity
    velocity_ = glm::vec3(newVelocity.GetX(), newVelocity.GetY(), newVelocity.GetZ());

    // Update character
    const BroadPhaseLayerFilter& bpFilter = system->GetDefaultBroadPhaseLayerFilter(NextLayers::MOVING);
    const ObjectLayerFilter& objFilter = system->GetDefaultLayerFilter(NextLayers::MOVING);

    ch->ExtendedUpdate(deltaSeconds,
        Vec3(0, gravity, 0),
        context_->updateSettings,
        bpFilter,
        objFilter,
        {},
        {},
        *tempAlloc);
#else
    (void)inputDirection;
    (void)speed;
    (void)jump;
    (void)deltaSeconds;
#endif
}

glm::vec3 NextCharacterController::GetPosition() const
{
#if WITH_PHYSIC
    if (context_ && context_->character)
    {
        RVec3 pos = context_->character->GetPosition();
        return glm::vec3(static_cast<float>(pos.GetX()),
                         static_cast<float>(pos.GetY()),
                         static_cast<float>(pos.GetZ()));
    }
#endif
    return glm::vec3(0.0f);
}

glm::vec3 NextCharacterController::GetLinearVelocity() const
{
    return velocity_;
}

ECharacterGroundState NextCharacterController::GetGroundState() const
{
#if WITH_PHYSIC
    if (context_ && context_->character)
    {
        switch (context_->character->GetGroundState())
        {
        case CharacterVirtual::EGroundState::OnGround:
            return ECharacterGroundState::OnGround;
        case CharacterVirtual::EGroundState::OnSteepGround:
            return ECharacterGroundState::OnSteepGround;
        case CharacterVirtual::EGroundState::NotSupported:
            return ECharacterGroundState::NotSupported;
        case CharacterVirtual::EGroundState::InAir:
            return ECharacterGroundState::InAir;
        }
    }
#endif
    return ECharacterGroundState::InAir;
}

bool NextCharacterController::IsOnGround() const
{
    return GetGroundState() == ECharacterGroundState::OnGround;
}

bool NextCharacterController::IsValid() const
{
#if WITH_PHYSIC
    return context_ && context_->character;
#else
    return false;
#endif
}
