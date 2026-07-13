#include "Engine/Runtime/Reflection/ReflectionRegistry.hpp"
#include "Engine/Runtime/Reflection/GlmTypeSupport.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/EnvironmentComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Components/SceneReferenceComponent.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Core/Node.hpp"

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
