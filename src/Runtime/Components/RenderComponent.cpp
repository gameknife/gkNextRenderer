#include "RenderComponent.h"
#include "Runtime/Reflection/PropertyMeta.h"
#include <entt/meta/factory.hpp>

namespace Runtime
{
    void RenderComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;
        
        entt::meta_factory<RenderComponent>()
            .type("RenderComponent"_hs)
            // Visible property - editable (use string literal for name)
            .data<&RenderComponent::SetVisible, &RenderComponent::GetVisible>("Visible")
                .custom<PropertyMeta>(PropertyPresets::Editable("Visible", "Rendering", "Whether the object is visible"))
            // RayCastVisible property - editable  
            .data<&RenderComponent::SetRayCastVisible, &RenderComponent::GetRayCastVisible>("RayCastVisible")
                .custom<PropertyMeta>(PropertyPresets::Editable("Raycast Visible", "Rendering", "Whether the object is visible to raycasts"))
            // ModelId property - read-only (set through scene loading)
            .data<nullptr, &RenderComponent::GetModelId>("ModelId")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Model ID", "Mesh", "The model resource ID"))
            // SkinIndex property - read-only
            .data<nullptr, &RenderComponent::GetSkinIndex>("SkinIndex")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Skin Index", "Animation", "Skinning data index for skeletal animation"))
            // Materials array - editable (array of material indices)
            .data<&RenderComponent::SetMaterials, &RenderComponent::GetMaterials>("Materials")
                .custom<PropertyMeta>(PropertyPresets::Editable("Materials", "Rendering", "Material indices for each submesh"));
    }
}
