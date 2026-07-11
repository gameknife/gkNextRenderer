#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/SplatLoader/GaussianSplatComponent.h"
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
            .data<&GaussianSplatComponent::SetCastShadow,
                  &GaussianSplatComponent::GetCastShadow>("CastShadow")
                .custom<PropertyMeta>(PropertyPresets::Editable("Cast Shadow", "Gaussian Splat",
                                                                 "Whether the generated proxy casts scene shadows"))
            .data<&GaussianSplatComponent::SetRayTraceOccluder,
                  &GaussianSplatComponent::GetRayTraceOccluder>("RayTraceOccluder")
                .custom<PropertyMeta>(PropertyPresets::Editable("Ray Trace Occluder", "Gaussian Splat",
                                                                 "Whether the generated proxy participates in ray occlusion"))
            .data<&GaussianSplatComponent::SetReceiveLighting,
                  &GaussianSplatComponent::GetReceiveLighting>("ReceiveLighting")
                .custom<PropertyMeta>(PropertyPresets::Editable("Receive Lighting", "Gaussian Splat",
                                                                 "Whether splat shading may be modulated by scene lighting"))
            .data<&GaussianSplatComponent::SetLightingStrength,
                  &GaussianSplatComponent::GetLightingStrength>("LightingStrength")
                .custom<PropertyMeta>(PropertyPresets::Editable("Lighting Strength", "Gaussian Splat",
                                                                 "How strongly scene lighting affects splat shading"))
            .data<&GaussianSplatComponent::SetProxyDensityScale,
                  &GaussianSplatComponent::GetProxyDensityScale>("ProxyDensityScale")
                .custom<PropertyMeta>(PropertyPresets::Editable("Proxy Density Scale", "Gaussian Splat",
                                                                 "Density multiplier used when generating the hidden proxy mesh"))
            .data<&GaussianSplatComponent::SetProxyAlphaThreshold,
                  &GaussianSplatComponent::GetProxyAlphaThreshold>("ProxyAlphaThreshold")
                .custom<PropertyMeta>(PropertyPresets::Editable("Proxy Alpha Threshold", "Gaussian Splat",
                                                                 "Iso alpha threshold used by the hidden proxy mesh"))
            .func<&GaussianSplatComponent::ToggleVisible>("ToggleVisible");
    }
}
