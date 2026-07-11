#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Reflection/GlmTypeSupport.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/EnvironmentComponent.h"
#include "Engine/Runtime/Components/PhysicsComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Components/SceneReferenceComponent.h"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Node.h"

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
        Runtime::EnvironmentComponent::RegisterReflection();
        Runtime::PhysicsComponent::RegisterReflection();
        Runtime::SkinnedMeshComponent::RegisterReflection();
        Runtime::SceneReferenceComponent::RegisterReflection();
        Assets::Node::RegisterReflection();
        
        NextEngine::RegisterReflection();
        Assets::Scene::RegisterReflection();
        
        sReflectionInitialized = true;
    }

    bool IsReflectionInitialized()
    {
        return sReflectionInitialized;
    }
}
