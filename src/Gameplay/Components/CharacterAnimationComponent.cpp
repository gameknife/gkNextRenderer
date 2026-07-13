#include "Gameplay/Components/CharacterAnimationComponent.h"

#include "Gameplay/Components/CharacterGameplayComponent.h"
#include "Gameplay/Gameplay/GameplayMath.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Reflection/PropertyMeta.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include <entt/meta/factory.hpp>

namespace NextGameplay
{
    namespace
    {
        glm::vec2 NormalizePlanarOrFallback(const glm::vec3& value, const glm::vec2& fallback)
        {
            const glm::vec2 planar(value.x, value.z);
            const float length = glm::length(planar);
            if (length <= 0.001f)
            {
                return fallback;
            }

            return planar / length;
        }
    }

    void CharacterAnimationComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;

        entt::meta_factory<ECharacterAnimState>()
            .type("ECharacterAnimState"_hs)
            .data<ECharacterAnimState::Idle>("Idle")
            .data<ECharacterAnimState::WalkForward>("WalkForward")
            .data<ECharacterAnimState::WalkBackward>("WalkBackward")
            .data<ECharacterAnimState::WalkStrafeLeft>("WalkStrafeLeft")
            .data<ECharacterAnimState::WalkStrafeRight>("WalkStrafeRight")
            .data<ECharacterAnimState::RunForward>("RunForward")
            .data<ECharacterAnimState::RunBackward>("RunBackward")
            .data<ECharacterAnimState::RunStrafeLeft>("RunStrafeLeft")
            .data<ECharacterAnimState::RunStrafeRight>("RunStrafeRight")
            .data<ECharacterAnimState::JumpStart>("JumpStart")
            .data<ECharacterAnimState::JumpLoop>("JumpLoop")
            .data<ECharacterAnimState::JumpLand>("JumpLand");

        entt::meta_factory<CharacterAnimationComponent>()
            .type("CharacterAnimationComponent"_hs)
            .data<nullptr, &CharacterAnimationComponent::GetAnimState>("AnimState")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Anim State", "Animation",
                                                                "Current locomotion animation state"))
            .data<nullptr, &CharacterAnimationComponent::GetCurrentClipName>("CurrentClip")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Current Clip", "Animation",
                                                                "Current runtime animation clip name"));
    }

    void CharacterAnimationComponent::SetBlendTuning(float walkStrafePlaySpeed, float runBackwardPlaySpeed,
                                                     float jumpStartHoldTime, float jumpLandHoldTime)
    {
        walkStrafePlaySpeed_ = walkStrafePlaySpeed;
        runBackwardPlaySpeed_ = runBackwardPlaySpeed;
        jumpStartHoldTime_ = jumpStartHoldTime;
        jumpLandHoldTime_ = jumpLandHoldTime;
    }

    void CharacterAnimationComponent::ClearRuntimeState()
    {
        animIdle_.clear();
        animWalkForward_.clear();
        animWalkBackward_.clear();
        animStrafeLeft_.clear();
        animStrafeRight_.clear();
        animRunForward_.clear();
        animRunBackward_.clear();
        animRunStrafeLeft_.clear();
        animRunStrafeRight_.clear();
        animJumpStart_.clear();
        animJumpLoop_.clear();
        animJumpLand_.clear();
        currentClipName_.clear();
        wasOnGroundLastFrame_ = true;
        jumpStartHoldTimeRemaining_ = 0.0f;
        jumpLandHoldTimeRemaining_ = 0.0f;
        animState_ = ECharacterAnimState::Idle;
    }

    void CharacterAnimationComponent::MapAnimationNames(const std::vector<std::string>& names)
    {
        auto findFirst = [&names](std::initializer_list<const char*> candidates) -> std::string
        {
            for (const char* candidate : candidates)
            {
                auto it = std::find(names.begin(), names.end(), candidate);
                if (it != names.end())
                {
                    return *it;
                }
            }
            return {};
        };

        animIdle_ = findFirst({"Idle_A", "Idle_B"});
        animWalkForward_ = findFirst({"Walking_B", "Walking_A", "Walking_C"});
        animWalkBackward_ = findFirst({"Walking_Backwards", "Walking_B", "Walking_A"});
        animStrafeLeft_ = findFirst({"Running_Strafe_Left", "Dodge_Left"});
        animStrafeRight_ = findFirst({"Running_Strafe_Right", "Dodge_Right"});
        animRunForward_ = findFirst({"Running_B", "Running_A"});
        animRunBackward_ = animWalkBackward_;
        animRunStrafeLeft_ = animStrafeLeft_;
        animRunStrafeRight_ = animStrafeRight_;
        animJumpStart_ = findFirst({"Jump_Start", "Jump_Full_Short", "Jump_Full_Long"});
        animJumpLoop_ = findFirst({"Jump_Idle", "Jump_Full_Long", "Jump_Full_Short"});
        animJumpLand_ = findFirst({"Jump_Land", "Jump_Full_Short"});

        if (animIdle_.empty() && !names.empty())
        {
            animIdle_ = names.front();
        }
        if (animWalkForward_.empty())
        {
            animWalkForward_ = animIdle_;
        }
        if (animWalkBackward_.empty())
        {
            animWalkBackward_ = animWalkForward_;
        }
        if (animStrafeLeft_.empty())
        {
            animStrafeLeft_ = animWalkForward_;
        }
        if (animStrafeRight_.empty())
        {
            animStrafeRight_ = animWalkForward_;
        }
        if (animRunForward_.empty())
        {
            animRunForward_ = animWalkForward_;
        }
        if (animRunBackward_.empty())
        {
            animRunBackward_ = animWalkBackward_;
        }
        if (animRunStrafeLeft_.empty())
        {
            animRunStrafeLeft_ = animStrafeLeft_;
        }
        if (animRunStrafeRight_.empty())
        {
            animRunStrafeRight_ = animStrafeRight_;
        }
        if (animJumpStart_.empty())
        {
            animJumpStart_ = animJumpLoop_.empty() ? animIdle_ : animJumpLoop_;
        }
        if (animJumpLoop_.empty())
        {
            animJumpLoop_ = animJumpStart_;
        }
        if (animJumpLand_.empty())
        {
            animJumpLand_ = animJumpLoop_;
        }
    }

    bool CharacterAnimationComponent::UpdateAnimation(CharacterGameplayComponent& gameplayComponent,
                                                       const FCharacterAnimationUpdateInput& input)
    {
        if (gameplayComponent.skinnedMeshComps.empty())
        {
            return false;
        }

        jumpStartHoldTimeRemaining_ = std::max(0.0f, jumpStartHoldTimeRemaining_ - input.deltaSeconds);
        jumpLandHoldTimeRemaining_ = std::max(0.0f, jumpLandHoldTimeRemaining_ - input.deltaSeconds);

        if (input.onGround && !wasOnGroundLastFrame_)
        {
            jumpLandHoldTimeRemaining_ = jumpLandHoldTime_;
        }
        if (!input.onGround && wasOnGroundLastFrame_ && input.velocity.y > 0.1f)
        {
            jumpStartHoldTimeRemaining_ = jumpStartHoldTime_;
            jumpLandHoldTimeRemaining_ = 0.0f;
        }

        const glm::vec2 horizontalVelocity(input.velocity.x, input.velocity.z);
        const float horizontalSpeed = glm::length(horizontalVelocity);
        const glm::vec3 commandedMoveDir = NormalizeHorizontalOrZero(input.commandedMoveDir);
        const glm::vec2 commandedHorizontalVelocity(commandedMoveDir.x * input.desiredSpeed,
                                                    commandedMoveDir.z * input.desiredSpeed);
        const glm::vec2 forward2D = NormalizePlanarOrFallback(input.referenceForward, glm::vec2(0.0f, 1.0f));
        const glm::vec2 right2D = NormalizePlanarOrFallback(input.referenceRight, glm::vec2(1.0f, 0.0f));
        const float actualForwardSpeed = glm::dot(horizontalVelocity, forward2D);
        const float actualRightSpeed = glm::dot(horizontalVelocity, right2D);
        const float intendedForwardSpeed = glm::dot(commandedHorizontalVelocity, forward2D);
        const float intendedRightSpeed = glm::dot(commandedHorizontalVelocity, right2D);
        const float localForwardSpeed =
            std::abs(actualForwardSpeed) >= std::abs(intendedForwardSpeed) * 0.65f ? actualForwardSpeed : intendedForwardSpeed;
        const float localRightSpeed =
            std::abs(actualRightSpeed) >= std::abs(intendedRightSpeed) * 0.65f ? actualRightSpeed : intendedRightSpeed;
        const float movementSignal = std::max(horizontalSpeed, glm::length(commandedHorizontalVelocity));

        ECharacterAnimState newState = ECharacterAnimState::Idle;
        std::string animationToPlay = animIdle_;
        bool loop = true;
        float playSpeed = 1.0f;

        if (!input.onGround)
        {
            if (jumpStartHoldTimeRemaining_ > 0.0f && input.velocity.y >= 0.0f)
            {
                newState = ECharacterAnimState::JumpStart;
                animationToPlay = animJumpStart_;
                loop = false;
            }
            else
            {
                newState = ECharacterAnimState::JumpLoop;
                animationToPlay = animJumpLoop_;
            }
        }
        else if (jumpLandHoldTimeRemaining_ > 0.0f)
        {
            newState = ECharacterAnimState::JumpLand;
            animationToPlay = animJumpLand_;
            loop = false;
        }
        else if (movementSignal > 0.35f)
        {
            const bool strafeDominant = std::abs(localRightSpeed) > std::abs(localForwardSpeed) * 1.1f;
            const bool backwardDominant = !strafeDominant && localForwardSpeed < -0.2f;

            switch (input.policy)
            {
            case ECharacterAnimationPolicy::PlayerMoveAligned:
                if (input.sprinting)
                {
                    newState = ECharacterAnimState::RunForward;
                    animationToPlay = animRunForward_;
                }
                else
                {
                    newState = ECharacterAnimState::WalkForward;
                    animationToPlay = animWalkForward_;
                }
                break;
            case ECharacterAnimationPolicy::AIChase:
                newState = ECharacterAnimState::RunForward;
                animationToPlay = animRunForward_;
                break;
            case ECharacterAnimationPolicy::AIEvade:
                if (backwardDominant)
                {
                    newState = ECharacterAnimState::RunBackward;
                    animationToPlay = animRunBackward_;
                    playSpeed = runBackwardPlaySpeed_;
                }
                else if (strafeDominant)
                {
                    const bool movingRight = localRightSpeed > 0.0f;
                    newState = movingRight ? ECharacterAnimState::RunStrafeRight : ECharacterAnimState::RunStrafeLeft;
                    animationToPlay = movingRight ? animRunStrafeRight_ : animRunStrafeLeft_;
                }
                else
                {
                    newState = ECharacterAnimState::RunForward;
                    animationToPlay = animRunForward_;
                }
                break;
            case ECharacterAnimationPolicy::AIPatrol:
            case ECharacterAnimationPolicy::AIAttack:
                if (strafeDominant)
                {
                    const bool movingRight = localRightSpeed > 0.0f;
                    newState = movingRight ? ECharacterAnimState::RunStrafeRight : ECharacterAnimState::RunStrafeLeft;
                    animationToPlay = movingRight ? animRunStrafeRight_ : animRunStrafeLeft_;
                }
                else if (backwardDominant)
                {
                    newState = ECharacterAnimState::WalkBackward;
                    animationToPlay = animWalkBackward_;
                }
                else if (input.policy == ECharacterAnimationPolicy::AIPatrol)
                {
                    newState = ECharacterAnimState::WalkForward;
                    animationToPlay = animWalkForward_;
                }
                else
                {
                    newState = ECharacterAnimState::RunForward;
                    animationToPlay = animRunForward_;
                }
                break;
            case ECharacterAnimationPolicy::PlayerCameraRelative:
            default:
                if (strafeDominant)
                {
                    const bool moveRight = localRightSpeed > 0.0f;
                    if (input.sprinting)
                    {
                        newState = moveRight ? ECharacterAnimState::RunStrafeRight : ECharacterAnimState::RunStrafeLeft;
                        animationToPlay = moveRight ? animRunStrafeRight_ : animRunStrafeLeft_;
                    }
                    else
                    {
                        newState = moveRight ? ECharacterAnimState::WalkStrafeRight : ECharacterAnimState::WalkStrafeLeft;
                        animationToPlay = moveRight ? animStrafeRight_ : animStrafeLeft_;
                        playSpeed = walkStrafePlaySpeed_;
                    }
                }
                else if (backwardDominant)
                {
                    if (input.sprinting)
                    {
                        newState = ECharacterAnimState::RunBackward;
                        animationToPlay = animRunBackward_;
                        playSpeed = runBackwardPlaySpeed_;
                    }
                    else
                    {
                        newState = ECharacterAnimState::WalkBackward;
                        animationToPlay = animWalkBackward_;
                    }
                }
                else if (input.sprinting)
                {
                    newState = ECharacterAnimState::RunForward;
                    animationToPlay = animRunForward_;
                }
                else
                {
                    newState = ECharacterAnimState::WalkForward;
                    animationToPlay = animWalkForward_;
                }
                break;
            }
        }

        wasOnGroundLastFrame_ = input.onGround;
        currentClipName_ = animationToPlay;

        const bool currentClipMismatched =
            loop && gameplayComponent.primarySkinnedMeshComp &&
            gameplayComponent.primarySkinnedMeshComp->GetCurrentAnimationName() != animationToPlay;
        if (newState != animState_ || currentClipMismatched)
        {
            animState_ = newState;
            gameplayComponent.PlayAnimation(animationToPlay, loop, playSpeed);
        }

        return true;
    }
}
