#pragma once

#include "Engine/Assets/Core/Component.h"
#include "Engine/NextGameplay/Gameplay/GameplayTypes.h"
#include "Engine/Runtime/Reflection/ReflectionMacros.h"

namespace Assets
{
    class Node;
}

namespace Runtime
{
    class SkinnedMeshComponent;
}

namespace NextGameplay
{
    class CharacterGameplayComponent : public Assets::Component
    {
    public:
        REFLECT_COMPONENT(CharacterGameplayComponent)

        CharacterGameplayComponent() = default;

        void SetFirstPersonMode(bool enabled) { firstPersonMode_ = enabled; }
        bool GetFirstPersonMode() const { return firstPersonMode_; }

        void SetFootIKEnabled(bool enabled) { footIKEnabled_ = enabled; }
        bool GetFootIKEnabled() const { return footIKEnabled_; }

        void SetMovementMode(ECharacterMovementMode mode) { movementMode_ = mode; }
        ECharacterMovementMode GetMovementMode() const { return movementMode_; }

        void SetEyeHeight(float height) { eyeHeight_ = height; }
        float GetEyeHeight() const { return eyeHeight_; }

        void SetModelLoaded(bool loaded) { modelLoaded = loaded; }
        bool GetModelLoaded() const { return modelLoaded; }

        void ClearRuntimeState();
        void SetSkinnedMeshComponents(std::vector<Runtime::SkinnedMeshComponent*> components);
        void PlayAnimation(const std::string& name, bool loop, float playSpeed = 1.0f) const;
        void ConfigureFootPlacementIK(bool enabled, bool debugDraw, float weight) const;

        // Runtime-only state owned by gameplay systems.
        std::shared_ptr<Assets::Node> visualRoot;
        std::shared_ptr<Assets::Node> skinnedRoot;
        Runtime::SkinnedMeshComponent* primarySkinnedMeshComp = nullptr;
        std::vector<Runtime::SkinnedMeshComponent*> skinnedMeshComps;
        std::string appendRootName = "Mannequin_Medium";
        bool modelLoaded = false;
        bool modelLoadRequested = false;
        float facingYaw = 0.0f;

    private:
        bool firstPersonMode_ = false;
        bool footIKEnabled_ = true;
        ECharacterMovementMode movementMode_ = ECharacterMovementMode::CameraAligned;
        float eyeHeight_ = 1.55f;
    };
}
