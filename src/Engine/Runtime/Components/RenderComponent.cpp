#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Reflection/PropertyMeta.hpp"
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
            .data<&RenderComponent::SetMainVisible, &RenderComponent::GetMainVisible>("MainVisible")
                .custom<PropertyMeta>(PropertyPresets::Editable("Main Visible", "Rendering", "Whether the object is drawn in the main visibility pass"))
            // RayCastVisible property - editable  
            .data<&RenderComponent::SetRayCastVisible, &RenderComponent::GetRayCastVisible>("RayCastVisible")
                .custom<PropertyMeta>(PropertyPresets::Editable("Raycast Visible", "Rendering", "Whether the object is visible to raycasts"))
            .data<&RenderComponent::SetRayTraceVisible, &RenderComponent::GetRayTraceVisible>("RayTraceVisible")
                .custom<PropertyMeta>(PropertyPresets::Editable("Ray Trace Visible", "Rendering", "Whether the object participates in GPU ray tracing acceleration structures"))
            .data<&RenderComponent::SetCastShadows, &RenderComponent::GetCastShadows>("CastShadows")
                .custom<PropertyMeta>(PropertyPresets::Editable("Cast Shadows", "Rendering", "Whether the object casts shadows"))
            .data<&RenderComponent::SetReceiveGI, &RenderComponent::GetReceiveGI>("ReceiveGI")
                .custom<PropertyMeta>(PropertyPresets::Editable("Receive GI", "Rendering", "Whether the object receives global illumination"))
            // ModelId property - read-only (set through scene loading)
            .data<nullptr, &RenderComponent::GetModelId>("ModelId")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Model ID", "Mesh", "The model resource ID"))
            // SkinIndex property - read-only
            .data<nullptr, &RenderComponent::GetSkinIndex>("SkinIndex")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Skin Index", "Animation", "Skinning data index for skeletal animation"))
            // Materials array - editable (array of material indices)
            .data<&RenderComponent::SetMaterials, &RenderComponent::GetMaterials>("Materials")
                .custom<PropertyMeta>(PropertyPresets::Editable("Materials", "Rendering", "Material indices for each submesh"))
            // Methods for JS
            .func<&RenderComponent::ToggleVisible>("ToggleVisible")
            .func<&RenderComponent::ToggleRayCastVisible>("ToggleRayCastVisible");
    }
}
