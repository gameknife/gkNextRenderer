#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Reflection/PropertyMeta.h"
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
                .custom<PropertyMeta>(PropertyPresets::Editable("Physics Offset", "Physics", "Offset from node origin for physics body"))
            .data<&PhysicsComponent::SetSimulatePhysics, &PhysicsComponent::GetSimulatePhysics>("SimulatePhysics")
                .custom<PropertyMeta>(PropertyPresets::Editable("Simulate Physics", "Physics", "Enable dynamic rigid-body simulation"))
            .data<&PhysicsComponent::SetPhysicsMaterial, &PhysicsComponent::GetPhysicsMaterial>("PhysicsMaterial")
                .custom<PropertyMeta>(PropertyPresets::Editable("Physics Material", "Physics", "Physics material preset"))
            .data<&PhysicsComponent::SetLinearDamping, &PhysicsComponent::GetLinearDamping>("LinearDamping")
                .custom<PropertyMeta>(PropertyPresets::Editable("Linear Damping", "Physics", "Linear velocity damping"))
            .data<&PhysicsComponent::SetAngularDamping, &PhysicsComponent::GetAngularDamping>("AngularDamping")
                .custom<PropertyMeta>(PropertyPresets::Editable("Angular Damping", "Physics", "Angular velocity damping"))
            .data<&PhysicsComponent::SetEnableGravity, &PhysicsComponent::GetEnableGravity>("EnableGravity")
                .custom<PropertyMeta>(PropertyPresets::Editable("Enable Gravity", "Physics", "Apply gravity to this body"))
            .data<&PhysicsComponent::SetCollisionPresets, &PhysicsComponent::GetCollisionPresets>("CollisionPresets")
                .custom<PropertyMeta>(PropertyPresets::Editable("Collision Presets", "Physics", "Collision response preset"));
            
        // Note: PhysicsBody ID is internal and not exposed to editor
    }
}
