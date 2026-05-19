#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Component.h"
#include "Engine/NextGameplay/Gameplay/GameplayTypes.h"
#include "Engine/Runtime/Reflection/ReflectionMacros.h"

#include <glm/glm.hpp>

namespace NextGameplay
{
    class CharacterGameplayComponent;

    enum class ECharacterAnimationPolicy
    {
        PlayerCameraRelative,
        PlayerMoveAligned,
        AIPatrol,
        AIChase,
        AIEvade,
        AIAttack,
    };

    struct FCharacterAnimationUpdateInput
    {
        glm::vec3 velocity{0.0f};
        glm::vec3 referenceForward{0.0f, 0.0f, 1.0f};
        glm::vec3 referenceRight{1.0f, 0.0f, 0.0f};
        glm::vec3 commandedMoveDir{0.0f};
        float desiredSpeed = 0.0f;
        float deltaSeconds = 0.0f;
        bool onGround = true;
        bool sprinting = false;
        ECharacterAnimationPolicy policy = ECharacterAnimationPolicy::PlayerCameraRelative;
    };

    class CharacterAnimationComponent : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(CharacterAnimationComponent)

        CharacterAnimationComponent() = default;

        ECharacterAnimState GetAnimState() const { return animState_; }
        const std::string& GetCurrentClipName() const { return currentClipName_; }
        const std::string& GetIdleAnimationName() const { return animIdle_; }

        void SetBlendTuning(float walkStrafePlaySpeed, float runBackwardPlaySpeed, float jumpStartHoldTime,
                            float jumpLandHoldTime);
        void ClearRuntimeState();
        void MapAnimationNames(const std::vector<std::string>& names);
        bool UpdateAnimation(CharacterGameplayComponent& gameplayComponent,
                             const FCharacterAnimationUpdateInput& input, bool footIKEnabled, bool debugDraw);

    private:
        std::string animIdle_;
        std::string animWalkForward_;
        std::string animWalkBackward_;
        std::string animStrafeLeft_;
        std::string animStrafeRight_;
        std::string animRunForward_;
        std::string animRunBackward_;
        std::string animRunStrafeLeft_;
        std::string animRunStrafeRight_;
        std::string animJumpStart_;
        std::string animJumpLoop_;
        std::string animJumpLand_;
        std::string currentClipName_;
        bool wasOnGroundLastFrame_ = true;
        float jumpStartHoldTimeRemaining_ = 0.0f;
        float jumpLandHoldTimeRemaining_ = 0.0f;
        float walkStrafePlaySpeed_ = 0.82f;
        float runBackwardPlaySpeed_ = 1.35f;
        float jumpStartHoldTime_ = 0.12f;
        float jumpLandHoldTime_ = 0.18f;
        ECharacterAnimState animState_ = ECharacterAnimState::Idle;
    };
}
