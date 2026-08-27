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

#include <entt/core/hashed_string.hpp>
#include <spdlog/spdlog.h>

namespace Reflection
{
    namespace
    {
        bool sReflectionInitialized = false;
        std::vector<FReflectedType> sReflectedTypes;

        /// Registers one type and records it in the same statement, so the list and the
        /// registrations cannot disagree.
        template <typename T>
        void Register(const char* name, bool isComponent)
        {
            T::RegisterReflection();

            const entt::meta_type meta = entt::resolve<T>();
            // Script code addresses a component by the hash of this name, resolved at code
            // generation time. If the type were registered under a different string the hash would
            // not match and every property lookup would fail while everything still compiled.
            if (meta.id() != entt::hashed_string::value(name))
            {
                SPDLOG_ERROR("reflection: {} was registered under a different name; component "
                             "property access will not resolve", name);
            }

            sReflectedTypes.push_back(FReflectedType{name, meta, isComponent});
        }
    }

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
        Register<Runtime::RenderComponent>("RenderComponent", true);
        Register<Runtime::EnvironmentComponent>("EnvironmentComponent", true);
        Register<Runtime::LightComponent>("LightComponent", true);
        Register<Runtime::PhysicsComponent>("PhysicsComponent", true);
        Register<Runtime::SkinnedMeshComponent>("SkinnedMeshComponent", true);
        Register<Runtime::SceneReferenceComponent>("SceneReferenceComponent", true);
        Register<Runtime::TerrainComponent>("TerrainComponent", true);
        Register<Assets::Node>("Node", false);

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

    const std::vector<FReflectedType>& GetReflectedTypes()
    {
        return sReflectedTypes;
    }
}
