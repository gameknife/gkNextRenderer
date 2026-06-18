#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Components/GaussianSplatComponent.h"
#include "Engine/Runtime/Reflection/PropertyMeta.h"

#include <entt/meta/factory.hpp>

namespace Runtime
{
    void GaussianSplatComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;

        entt::meta_factory<GaussianSplatComponent>()
            .type("GaussianSplatComponent"_hs)
            .data<nullptr, &GaussianSplatComponent::GetSplatModelId>("SplatModelId")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Splat Model ID", "Gaussian Splat",
                                                                 "The Gaussian splat resource ID"))
            .data<&GaussianSplatComponent::SetVisible, &GaussianSplatComponent::GetVisible>("Visible")
                .custom<PropertyMeta>(PropertyPresets::Editable("Visible", "Gaussian Splat",
                                                                 "Whether the splat asset is rendered"))
            .data<&GaussianSplatComponent::SetRayCastVisible,
                  &GaussianSplatComponent::GetRayCastVisible>("RayCastVisible")
                .custom<PropertyMeta>(PropertyPresets::Editable("Raycast Visible", "Gaussian Splat",
                                                                 "Whether the splat bounds can be selected"))
            .data<&GaussianSplatComponent::SetOpacityScale,
                  &GaussianSplatComponent::GetOpacityScale>("OpacityScale")
                .custom<PropertyMeta>(PropertyPresets::Editable("Opacity Scale", "Gaussian Splat",
                                                                 "Multiplier applied to splat opacity"))
            .func<&GaussianSplatComponent::ToggleVisible>("ToggleVisible");
    }
}
