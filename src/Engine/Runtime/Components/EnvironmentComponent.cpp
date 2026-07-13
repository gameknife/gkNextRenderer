#include "Engine/Runtime/Components/EnvironmentComponent.hpp"

#include "Engine/Runtime/Reflection/PropertyMeta.hpp"

#include <entt/meta/factory.hpp>

namespace Runtime
{
    void EnvironmentComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;

        entt::meta_factory<EnvironmentComponent>()
            .type("EnvironmentComponent"_hs)
            .data<&EnvironmentComponent::SetHasSky, &EnvironmentComponent::GetHasSky>("HasSky")
                .custom<PropertyMeta>(PropertyPresets::Editable("Has Sky", "Environment", "Enable HDR sky lighting"))
            .data<&EnvironmentComponent::SetSkyIdx, &EnvironmentComponent::GetSkyIdx>("SkyIdx")
                .custom<PropertyMeta>(PropertyPresets::Editable("Sky Index", "Environment", "HDR sky texture index"))
            .data<&EnvironmentComponent::SetSkyRotation, &EnvironmentComponent::GetSkyRotation>("SkyRotation")
                .custom<PropertyMeta>(PropertyPresets::Editable("Sky Rotation", "Environment", "Horizontal sky rotation"))
            .data<&EnvironmentComponent::SetSkyIntensity, &EnvironmentComponent::GetSkyIntensity>("SkyIntensity")
                .custom<PropertyMeta>(PropertyPresets::Editable("Sky Intensity", "Environment", "Sky lighting intensity"))
            .data<&EnvironmentComponent::SetHasSun, &EnvironmentComponent::GetHasSun>("HasSun")
                .custom<PropertyMeta>(PropertyPresets::Editable("Has Sun", "Environment", "Enable directional sunlight"))
            .data<&EnvironmentComponent::SetSunRotation, &EnvironmentComponent::GetSunRotation>("SunRotation")
                .custom<PropertyMeta>(PropertyPresets::Editable("Sun Rotation", "Environment", "Sun azimuth rotation"))
            .data<&EnvironmentComponent::SetSunIntensity, &EnvironmentComponent::GetSunIntensity>("SunIntensity")
                .custom<PropertyMeta>(PropertyPresets::Editable("Sun Intensity", "Environment", "Sun light intensity"))
            .data<&EnvironmentComponent::SetControlSpeed, &EnvironmentComponent::GetControlSpeed>("ControlSpeed")
                .custom<PropertyMeta>(PropertyPresets::Editable("Control Speed", "Camera", "Default scene camera control speed"))
            .data<&EnvironmentComponent::SetGammaCorrection, &EnvironmentComponent::GetGammaCorrection>("GammaCorrection")
                .custom<PropertyMeta>(PropertyPresets::Editable("Gamma Correction", "Environment", "Enable gamma correction"));
    }
}
