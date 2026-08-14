#include "Engine/Runtime/Reflection/ReflectionRegistry.hpp"
#include "Engine/Runtime/Reflection/GlmTypeSupport.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/EnvironmentComponent.hpp"
#include "Engine/Runtime/Components/LightComponent.hpp"
#include "Engine/Runtime/Components/PhysicsComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Components/SceneReferenceComponent.hpp"
#include "Engine/Runtime/Components/TerrainComponent.hpp"
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
        Runtime::LightComponent::RegisterReflection();
        Runtime::PhysicsComponent::RegisterReflection();
        Runtime::SkinnedMeshComponent::RegisterReflection();
        Runtime::SceneReferenceComponent::RegisterReflection();
        Runtime::TerrainComponent::RegisterReflection();
        Assets::Node::RegisterReflection();

        // NextEngine and Assets::Scene are deliberately absent. Reflection owns component and node
        // *properties*; engine-level and scene-level *functions* are owned by the script binding
        // table (src/Modules/NextDotNet/EngineApi.def.h). They used to be registered here as well,
        // which is how the two sources drifted apart — see the design's section 4.4.

        sReflectionInitialized = true;
    }

    bool IsReflectionInitialized()
    {
        return sReflectionInitialized;
    }
}
