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

        /// Loads a game assembly into the running host, replacing whatever was loaded before.
        /// The path is relative to the managed root (<bin>/csharp), matching FConfig::gameAssembly.
        ///
        /// This is the same managed call hot reload has always made; the only new thing is that the
        /// caller chooses the path. Under NativeAOT the game is linked in and the path is ignored,
        /// so this reports success without doing anything — a NativeAOT host has exactly one game.
        bool LoadGameAssembly(const std::string& relativeAssembly, bool enableHotReload);

        /// Unloads the current game. Returns false when the managed side reported a failure;
        /// an uncollected load context is a success with a warning, see UnloadPendingStreak.
        bool UnloadGame();

        bool IsGameLoaded() const { return gameLoaded_; }

        /// Consecutive unloads that left the previous load context alive. One is unremarkable —
        /// the GC had not run yet. A growing streak is a managed leak that would eventually
        /// exhaust memory, so a host that swaps games repeatedly should stop and say so rather
        /// than keep loading.
        uint32_t UnloadPendingStreak() const { return unloadPendingStreak_; }

        /// Whether this backend can load a game chosen at runtime. False under NativeAOT.
        bool SupportsRuntimeGameSwitching() const;

        /// Stops delivering input to the running game without stopping the game itself.
        ///
        /// This is what an editor needs when the user ejects out of a Play session: the game keeps
        /// ticking so the scene stays alive to inspect, but WASD belongs to the editor camera again.
        /// Disabling also releases whatever was held down — otherwise the game would keep walking
        /// forward forever, having never seen the key-up.
        void SetInputEnabled(bool enabled);
        bool IsInputEnabled() const { return inputEnabled_; }

        /// Pauses or resumes game tick execution. While paused, the managed game's Tick is skipped,
        /// freezing entity movement, physics updates and gameplay logic.
        void SetPaused(bool paused);
        bool IsPaused() const { return isPaused_; }

        /// Poll-style gamepad axes are forwarded by the thin application host. They deliberately
        /// do not add another managed lifecycle hook: gameplay reads the latest values through
        /// Input.GetGamepadAxis just like keyboard state.
        void SetGamepadInput(int16_t leftStickX,
                             int16_t leftStickY,
                             int16_t rightStickX,
                             int16_t rightStickY,
                             int16_t leftTrigger,
                             int16_t rightTrigger);

        bool IsReady() const { return managed_ != nullptr; }
        const char* BackendName() const;

        /// Where published game assemblies live: <bin>/csharp, or GK_DOTNET_MANAGED_DIR when set.
        /// Static because callers need it before — and after — any runtime exists.
        static std::filesystem::path ManagedRoot();

        /// Publishes a managed project into the managed root, the way CMake does at build time.
        /// Development-only: it needs the .NET SDK and the source tree, and does nothing useful in
        /// an installed build. Returns false and fills outError when the SDK is missing or the
        /// publish fails, leaving the previously published assemblies untouched.
        static bool PublishProject(const std::string& projectRelativeToManagedSources,
                                   const std::string& outputSubdirectory,
                                   std::string& outError);

    private:
        bool EnsureManagedBuild();
        void TickHotReload(double deltaSeconds);
        std::filesystem::path ResolveGameAssemblyPath(const std::string& relativeAssembly) const;

        NextEngine& engine_;
        FConfig config_;
        FEngineApi engineApi_{};
        std::unique_ptr<IManagedHost> host_;
        const FManagedApi* managed_ = nullptr;
        std::filesystem::path gameAssemblyPath_;
        std::filesystem::file_time_type gameAssemblyTimestamp_{};
        double hotReloadElapsed_ = 0.0;
        bool gameLoaded_ = false;
        bool hotReloadEnabled_ = false;
        bool inputEnabled_ = true;
        bool isPaused_ = false;
        uint32_t unloadPendingStreak_ = 0;
    };
}
