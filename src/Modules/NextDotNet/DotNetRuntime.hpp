#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/Interface/ScriptRuntime.hpp"
#include "Modules/NextDotNet/EngineApi.hpp"
#include "Modules/NextDotNet/Host/IManagedHost.hpp"
#include "Modules/NextDotNet/NextDotNetModule.hpp"

#include <SDL3/SDL.h>

#include <filesystem>

namespace Modules::NextDotNet
{
    /// The engine-facing script runtime. Owns the managed host and translates engine events into
    /// the two function tables.
    ///
    /// The backend (CoreCLR or NativeAOT) is invisible here: everything that differs lives behind
    /// IManagedHost, and this class only asks it whether hot reload exists.
    class DotNetRuntime final : public Runtime::IScriptRuntime
    {
    public:
        DotNetRuntime(NextEngine& engine, FConfig config);
        ~DotNetRuntime() override;

        void Initialize() override;
        void Tick(double deltaSeconds) override;
        void HandleEvent(const SDL_Event& event) override;

        /// Raises a lifecycle hook. Returns true when the script reported the hook as consumed;
        /// only OnRenderUI acts on that, matching the QuickJS onRenderUI contract.
        bool CallLifecycleHook(EScriptHook hook, double deltaSeconds = 0.0);

        /// Runs the BeforeSceneRebuild hook with the SceneBuild bindings pointed at these vectors.
        /// Outside this call the SceneBuild namespace refuses to do anything.
        bool CallBeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                    std::vector<Assets::Model>& models,
                                    std::vector<Assets::FMaterial>& materials,
                                    std::vector<Assets::LightObject>& lights,
                                    std::vector<Assets::AnimationTrack>& tracks);

        bool TryGetOverrideCamera(Assets::Camera& outCamera) const;

        bool IsReady() const { return managed_ != nullptr; }
        const char* BackendName() const;

    private:
        bool EnsureManagedBuild();
        void TickHotReload(double deltaSeconds);
        std::filesystem::path ResolveGameAssemblyPath() const;

        NextEngine& engine_;
        FConfig config_;
        FEngineApi engineApi_{};
        std::unique_ptr<IManagedHost> host_;
        const FManagedApi* managed_ = nullptr;
        std::filesystem::path gameAssemblyPath_;
        std::filesystem::file_time_type gameAssemblyTimestamp_{};
        double hotReloadElapsed_ = 0.0;
    };
}
