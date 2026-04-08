#include "NextGameplay/Components/CharacterControlComponent.h"

#include "Runtime/Reflection/PropertyMeta.h"

#include <entt/meta/factory.hpp>

namespace NextGameplay
{
    void CharacterControlComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;

        entt::meta_factory<ECharacterControlSource>()
            .type("ECharacterControlSource"_hs)
            .data<ECharacterControlSource::Player>("Player")
            .data<ECharacterControlSource::AI>("AI");

        entt::meta_factory<CharacterControlComponent>()
            .type("CharacterControlComponent"_hs)
            .data<&CharacterControlComponent::SetEnabled, &CharacterControlComponent::GetEnabled>("Enabled")
                .custom<PropertyMeta>(PropertyPresets::Editable("Enabled", "Control",
                                                                "Whether control intent is consumed"))
            .data<&CharacterControlComponent::SetControlSource, &CharacterControlComponent::GetControlSource>(
                "ControlSource")
                .custom<PropertyMeta>(PropertyPresets::Editable("Control Source", "Control",
                                                                "Current character control provider"))
            .data<nullptr, &CharacterControlComponent::GetDesiredSpeed>("DesiredSpeed")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Desired Speed", "Control",
                                                                "Requested move speed from current controller"))
            .data<nullptr, &CharacterControlComponent::GetSprinting>("Sprinting")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Sprinting", "Control",
                                                                "Whether the current controller is sprinting"))
            .data<nullptr, &CharacterControlComponent::GetJumpRequested>("JumpRequested")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Jump Requested", "Control",
                                                                "Whether the controller requested a jump"));
    }

    void CharacterControlComponent::ClearFrameState()
    {
        moveIntent_ = glm::vec3(0.0f);
        desiredSpeed_ = 0.0f;
        sprinting_ = false;
        jumpRequested_ = false;
    }
}
