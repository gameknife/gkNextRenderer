#include "ReflectionRegistry.h"
#include "GlmTypeSupport.h"
#include "Runtime/Components/RenderComponent.h"
#include "Runtime/Components/PhysicsComponent.h"
#include "Runtime/Components/SkinnedMeshComponent.h"

namespace Reflection
{
    static bool sReflectionInitialized = false;

    void RegisterAllReflection()
    {
        if (sReflectionInitialized)
        {
            return;
        }
        
        // Register GLM types first
        RegisterGlmTypes();
        
        // Register container types for array reflection
        RegisterContainerTypes();
        
        // Register all component types
        Runtime::RenderComponent::RegisterReflection();
        Runtime::PhysicsComponent::RegisterReflection();
        Runtime::SkinnedMeshComponent::RegisterReflection();
        
        sReflectionInitialized = true;
    }

    bool IsReflectionInitialized()
    {
        return sReflectionInitialized;
    }
}
