#include "PhysicsComponent.h"
#include "Runtime/Reflection/PropertyMeta.h"
#include <entt/meta/factory.hpp>

namespace Runtime
{
    void PhysicsComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;
        
        // Register ENodeMobility enum first (use string literals for names)
        entt::meta_factory<ENodeMobility>()
            .type("ENodeMobility"_hs)
            .data<ENodeMobility::Static>("Static")
            .data<ENodeMobility::Dynamic>("Dynamic")
            .data<ENodeMobility::Kinematic>("Kinematic");
        
        // Register PhysicsComponent (use string literals for names)
        entt::meta_factory<PhysicsComponent>()
            .type("PhysicsComponent"_hs)
            // Mobility property - editable enum
            .data<&PhysicsComponent::SetMobility, &PhysicsComponent::GetMobility>("Mobility")
                .custom<PropertyMeta>(PropertyPresets::Editable("Mobility", "Physics", "Physics body mobility type"))
            // PhysicsOffset property - editable vec3
            .data<&PhysicsComponent::SetPhysicsOffset, &PhysicsComponent::GetPhysicsOffset>("PhysicsOffset")
                .custom<PropertyMeta>(PropertyPresets::Editable("Physics Offset", "Physics", "Offset from node origin for physics body"));
            
        // Note: PhysicsBody ID is internal and not exposed to editor
    }
}
