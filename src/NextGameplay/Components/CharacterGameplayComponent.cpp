#include "NextGameplay/Components/CharacterGameplayComponent.h"

#include "Runtime/Components/SkinnedMeshComponent.h"
#include "Runtime/Reflection/PropertyMeta.h"

#include <entt/meta/factory.hpp>

namespace NextGameplay
{
    void CharacterGameplayComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;

        entt::meta_factory<ECharacterMovementMode>()
            .type("ECharacterMovementMode"_hs)
            .data<ECharacterMovementMode::CameraAligned>("CameraAligned")
            .data<ECharacterMovementMode::MoveAligned>("MoveAligned");

        entt::meta_factory<CharacterGameplayComponent>()
            .type("CharacterGameplayComponent"_hs)
            .data<&CharacterGameplayComponent::SetFirstPersonMode, &CharacterGameplayComponent::GetFirstPersonMode>(
                "FirstPersonMode")
                .custom<PropertyMeta>(PropertyPresets::Editable("First Person", "Gameplay",
                                                                "Whether the actor is rendered in first-person"))
            .data<&CharacterGameplayComponent::SetFootIKEnabled, &CharacterGameplayComponent::GetFootIKEnabled>(
                "FootIKEnabled")
                .custom<PropertyMeta>(PropertyPresets::Editable("Foot IK", "Gameplay",
                                                                "Enable runtime foot placement IK"))
            .data<&CharacterGameplayComponent::SetMovementMode, &CharacterGameplayComponent::GetMovementMode>(
                "MovementMode")
                .custom<PropertyMeta>(PropertyPresets::Editable("Movement Mode", "Gameplay",
                                                                "How facing is resolved from input"))
            .data<&CharacterGameplayComponent::SetEyeHeight, &CharacterGameplayComponent::GetEyeHeight>("EyeHeight")
                .custom<PropertyMeta>(PropertyPresets::Range("Eye Height", "Gameplay", 0.0f, 4.0f,
                                                             "Eye offset used by gameplay camera and traces"))
            .data<nullptr, &CharacterGameplayComponent::GetModelLoaded>("ModelLoaded")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Model Loaded", "Gameplay",
                                                                "Whether the runtime skinned actor is ready"));
    }

    void CharacterGameplayComponent::ClearRuntimeState()
    {
        visualRoot.reset();
        skinnedRoot.reset();
        primarySkinnedMeshComp = nullptr;
        skinnedMeshComps.clear();
        modelLoaded = false;
        modelLoadRequested = false;
    }

    void CharacterGameplayComponent::SetSkinnedMeshComponents(std::vector<Runtime::SkinnedMeshComponent*> components)
    {
        skinnedMeshComps = std::move(components);
        primarySkinnedMeshComp = skinnedMeshComps.empty() ? nullptr : skinnedMeshComps.front();
    }

    void CharacterGameplayComponent::PlayAnimation(const std::string& name, bool loop, float playSpeed) const
    {
        if (name.empty())
        {
            return;
        }

        for (Runtime::SkinnedMeshComponent* skinnedMeshComp : skinnedMeshComps)
        {
            if (!skinnedMeshComp)
            {
                continue;
            }

            skinnedMeshComp->SetPlaySpeed(playSpeed);
            skinnedMeshComp->PlayAnimation(name, loop);
        }
    }

    void CharacterGameplayComponent::ConfigureFootPlacementIK(bool enabled, bool debugDraw, float weight) const
    {
        for (Runtime::SkinnedMeshComponent* skinnedMeshComp : skinnedMeshComps)
        {
            if (!skinnedMeshComp)
            {
                continue;
            }

            skinnedMeshComp->SetFootPlacementIKEnabled(enabled);
            skinnedMeshComp->SetFootPlacementIKWeight(weight);
            auto settings = skinnedMeshComp->GetFootPlacementIKSettings();
            settings.DebugDraw = debugDraw;
            skinnedMeshComp->SetFootPlacementIKSettings(settings);
        }
    }
}
