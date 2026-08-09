#include "Engine/Runtime/Components/LightComponent.hpp"

#include "Engine/Runtime/Reflection/PropertyMeta.hpp"

#include <entt/meta/factory.hpp>

namespace Runtime
{
    Assets::LightObject* LightComponent::PrimaryLight()
    {
        return lights_.empty() ? nullptr : &lights_.front();
    }

    const Assets::LightObject* LightComponent::PrimaryLight() const
    {
        return lights_.empty() ? nullptr : &lights_.front();
    }

    void LightComponent::SetLightType(uint32_t value)
    {
        if (auto* light = PrimaryLight()) light->lightType = value;
    }

    uint32_t LightComponent::GetLightType() const
    {
        const auto* light = PrimaryLight();
        return light ? light->lightType : Assets::LightTypeArea;
    }

    void LightComponent::SetMaterialIndex(uint32_t value)
    {
        if (auto* light = PrimaryLight()) light->lightMatIdx = value;
    }

    uint32_t LightComponent::GetMaterialIndex() const
    {
        const auto* light = PrimaryLight();
        return light ? light->lightMatIdx : 0u;
    }

    void LightComponent::SetP0(const glm::vec4& value)
    {
        if (auto* light = PrimaryLight()) light->p0 = value;
    }

    glm::vec4 LightComponent::GetP0() const
    {
        const auto* light = PrimaryLight();
        return light ? light->p0 : glm::vec4(0.0f);
    }

    void LightComponent::SetP1(const glm::vec4& value)
    {
        if (auto* light = PrimaryLight()) light->p1 = value;
    }

    glm::vec4 LightComponent::GetP1() const
    {
        const auto* light = PrimaryLight();
        return light ? light->p1 : glm::vec4(0.0f);
    }

    void LightComponent::SetP3(const glm::vec4& value)
    {
        if (auto* light = PrimaryLight()) light->p3 = value;
    }

    glm::vec4 LightComponent::GetP3() const
    {
        const auto* light = PrimaryLight();
        return light ? light->p3 : glm::vec4(0.0f);
    }

    void LightComponent::SetNormalArea(const glm::vec4& value)
    {
        if (auto* light = PrimaryLight()) light->normal_area = value;
    }

    glm::vec4 LightComponent::GetNormalArea() const
    {
        const auto* light = PrimaryLight();
        return light ? light->normal_area : glm::vec4(0.0f);
    }

    void LightComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;

        entt::meta_factory<LightComponent>()
            .type("LightComponent"_hs)
            .data<&LightComponent::SetEnabled, &LightComponent::GetEnabled>("Enabled")
                .custom<PropertyMeta>(PropertyPresets::Editable("Enabled", "Light", "Whether this light emits"))
            .data<nullptr, &LightComponent::GetLightCount>("LightCount")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Light Count", "Light", "Number of light primitives on this node"))
            .data<&LightComponent::SetLightType, &LightComponent::GetLightType>("LightType")
                .custom<PropertyMeta>(PropertyPresets::Range("Light Type", "Light", 0.0f, 1.0f, "0 = area, 1 = point"))
            .data<&LightComponent::SetMaterialIndex, &LightComponent::GetMaterialIndex>("MaterialIndex")
                .custom<PropertyMeta>(PropertyPresets::Editable("Material Index", "Light", "Diffuse-light material index"))
            .data<&LightComponent::SetP0, &LightComponent::GetP0>("P0")
                .custom<PropertyMeta>(PropertyPresets::Editable("P0 / Position", "Light Geometry", "Area corner or point position/radius"))
            .data<&LightComponent::SetP1, &LightComponent::GetP1>("P1")
                .custom<PropertyMeta>(PropertyPresets::Editable("P1", "Light Geometry", "Second area-light corner"))
            .data<&LightComponent::SetP3, &LightComponent::GetP3>("P3")
                .custom<PropertyMeta>(PropertyPresets::Editable("P3", "Light Geometry", "Third area-light corner"))
            .data<&LightComponent::SetNormalArea, &LightComponent::GetNormalArea>("NormalArea")
                .custom<PropertyMeta>(PropertyPresets::Editable("Normal / Area", "Light Geometry", "Local normal in xyz and sampling area in w"));
    }
}
