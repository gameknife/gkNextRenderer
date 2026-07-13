#include "Engine/Runtime/Components/SceneReferenceComponent.hpp"

#include "Engine/Runtime/Reflection/PropertyMeta.hpp"

#include <entt/meta/factory.hpp>

namespace Runtime
{
    std::string SceneReferenceComponent::GetStatusName() const
    {
        switch (status_)
        {
        case ESceneReferenceStatus::Unloaded:
            return "Unloaded";
        case ESceneReferenceStatus::Loaded:
            return "Loaded";
        case ESceneReferenceStatus::Missing:
            return "Missing";
        case ESceneReferenceStatus::CycleDetected:
            return "Cycle Detected";
        case ESceneReferenceStatus::Unsupported:
            return "Unsupported";
        case ESceneReferenceStatus::Failed:
            return "Failed";
        default:
            return "Unknown";
        }
    }

    void SceneReferenceComponent::RegisterReflection()
    {
        using namespace entt::literals;
        using namespace Reflection;

        entt::meta_factory<SceneReferenceComponent>()
            .type("SceneReferenceComponent"_hs)
            .data<&SceneReferenceComponent::SetAssetPath, &SceneReferenceComponent::GetAssetPath>("AssetPath")
                .custom<PropertyMeta>(PropertyPresets::Editable("Asset Path", "Scene Reference",
                                                                 "Referenced scene asset path"))
            .data<nullptr, &SceneReferenceComponent::GetStatusName>("Status")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Status", "Scene Reference",
                                                                 "Reference load status"))
            .data<nullptr, &SceneReferenceComponent::GetResolvedPath>("ResolvedPath")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Resolved Path", "Scene Reference",
                                                                 "Normalized referenced asset path"))
            .data<nullptr, &SceneReferenceComponent::GetError>("Error")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Error", "Scene Reference",
                                                                 "Last reference load error"))
            .data<nullptr, &SceneReferenceComponent::GetLoadedNodeCount>("LoadedNodeCount")
                .custom<PropertyMeta>(PropertyPresets::ReadOnly("Loaded Nodes", "Scene Reference",
                                                                 "Number of internal nodes loaded from the asset"));
    }
}
