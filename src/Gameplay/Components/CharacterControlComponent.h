#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Component.h"
#include "Gameplay/Gameplay/GameplayTypes.h"
#include "Engine/Runtime/Reflection/ReflectionMacros.h"

#include <glm/glm.hpp>

namespace NextGameplay
{
    class CharacterControlComponent : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(CharacterControlComponent)

        CharacterControlComponent() = default;

        void SetEnabled(bool enabled) { enabled_ = enabled; }
        bool GetEnabled() const { return enabled_; }

        void SetControlSource(ECharacterControlSource source) { controlSource_ = source; }
        ECharacterControlSource GetControlSource() const { return controlSource_; }

        void SetMoveIntent(const glm::vec3& moveIntent) { moveIntent_ = moveIntent; }
        const glm::vec3& GetMoveIntent() const { return moveIntent_; }

        void SetLookIntent(const glm::vec3& lookIntent) { lookIntent_ = lookIntent; }
        const glm::vec3& GetLookIntent() const { return lookIntent_; }

        void SetDesiredSpeed(float desiredSpeed) { desiredSpeed_ = desiredSpeed; }
        float GetDesiredSpeed() const { return desiredSpeed_; }

        void SetSprinting(bool sprinting) { sprinting_ = sprinting; }
        bool GetSprinting() const { return sprinting_; }

        void SetJumpRequested(bool requested) { jumpRequested_ = requested; }
        bool GetJumpRequested() const { return jumpRequested_; }
        bool ConsumeJumpRequested()
        {
            const bool requested = jumpRequested_;
            jumpRequested_ = false;
            return requested;
        }

        void ClearFrameState();

    private:
        bool enabled_ = true;
        ECharacterControlSource controlSource_ = ECharacterControlSource::Player;
        glm::vec3 moveIntent_{0.0f};
        glm::vec3 lookIntent_{0.0f, 0.0f, 1.0f};
        float desiredSpeed_ = 0.0f;
        bool sprinting_ = false;
        bool jumpRequested_ = false;
    };
}
