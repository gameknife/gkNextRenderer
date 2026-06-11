#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/AssetsFwd.hpp"

#include <initializer_list>
#include <string_view>

namespace Assets
{
    // Scene loader entry point for one file format. Signature mirrors
    // Runtime::Scene::SceneList::LoadScene so registered loaders can plug
    // straight into the dispatch there.
    using FSceneLoaderFn = std::function<bool(
        const std::string& filename, EnvironmentSetting& camera,
        std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models,
        std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
        std::vector<AnimationTrack>& tracks, std::vector<Skeleton>& skeletons)>;

    // Procedural scene builder (a ".proc" entry in the scene list).
    using FProcSceneFn = std::function<void(
        EnvironmentSetting& camera,
        std::vector<std::shared_ptr<Node>>& nodes, std::vector<Model>& models,
        std::vector<FMaterial>& materials, std::vector<LightObject>& lights,
        std::vector<AnimationTrack>& tracks)>;

    // Registry decoupling the engine core from optional scene loaders and
    // application demo scenes. glTF stays built into the core; modules
    // (Modules/LDrawLoader, Modules/ScadLoader, ...) and applications register
    // their formats / procedural scenes explicitly at startup.
    class FLoaderRegistry final
    {
    public:
        static FLoaderRegistry& Get();

        // extensions: lowercase, dot-prefixed (".ldr"). Registration order
        // defines the grouping order in the scene list.
        void RegisterSceneLoader(std::initializer_list<std::string_view> extensions, FSceneLoaderFn loadFn);
        void RegisterProcScene(std::string_view name, FProcSceneFn buildFn);

        bool SupportsExtension(std::string_view extension) const;
        // Sort key for scene list grouping: 0 for the first registered loader,
        // 1 for the second, ...; -1 when the extension is unknown.
        int GetExtensionOrder(std::string_view extension) const;
        std::vector<std::string> RegisteredExtensions() const;
        std::vector<std::string> ProcSceneNames() const;

        const FSceneLoaderFn* FindSceneLoader(std::string_view extension) const;
        const FProcSceneFn* FindProcScene(std::string_view name) const;

    private:
        struct FLoaderEntry
        {
            std::vector<std::string> extensions;
            FSceneLoaderFn load;
        };

        struct FProcEntry
        {
            std::string name;
            FProcSceneFn build;
        };

        std::vector<FLoaderEntry> loaders_;
        std::vector<FProcEntry> procScenes_;
    };
}
