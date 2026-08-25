#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Runtime/Config/ShowFlags.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Modules/NextDotNet/ManagedGameManifest.hpp"

#include <functional>

class NextEngine;

namespace Modules::NextDotNet
{
    class DotNetRuntime;

    enum class EGameSessionState
    {
        Idle,
        Loading,
        Playing,
        Unloading,
    };

    /// Owns one managed game's lifetime inside a running engine: load it, run it, unload it, and
    /// leave the world in the state it was found in.
    ///
    /// Deliberately not a NextGameInstanceBase. A per-game executable drives it through
    /// ManagedGameHostInstance, gkNextLauncher drives it from its menu, and gkNextEditor will drive
    /// it from a Play button — none of them should have to reimplement the state machine or the
    /// world reset. See docs/designs/managed-game-launcher-design.md sections 4.3 and 4.4.
    class ManagedGameSession final
    {
    public:
        explicit ManagedGameSession(NextEngine& engine);
        ~ManagedGameSession();

        ManagedGameSession(const ManagedGameSession&) = delete;
        ManagedGameSession& operator=(const ManagedGameSession&) = delete;

        /// Native modules this host was linked with. A manifest asking for anything outside this
        /// set is refused before it can half-load: modules are static libraries and cannot be
        /// acquired at runtime.
        void SetLinkedModules(std::vector<std::string> modules);
        bool AreRequirementsMet(const FManagedGameManifest& manifest, std::string* outMissing) const;

        /// CVars the world baseline must not capture or restore.
        ///
        /// A host drives this session through cvars of its own — which game to run, whether to
        /// eject. Those describe the host, not the world the game is allowed to disturb. Restoring
        /// one on unload writes the *previous* game's id back and re-triggers the host's own
        /// callback, so stopping a game immediately restarts it. Exclude them.
        void SetBaselineExcludedCVars(std::vector<std::string> names);

        /// Queues a load for the next frame boundary. Any game currently running is unloaded first.
        /// Never unloads on the managed callstack: a game asking to be replaced from inside its own
        /// Tick would be pulling its own AssemblyLoadContext out from under itself.
        void RequestLoad(FManagedGameManifest manifest);

        /// Queues an unload, returning the world to the state it had before the game was loaded.
        ///
        /// `returnScene` is what gets loaded once the game is gone; empty means the neutral empty
        /// scene. An editor passes the scene it was editing before Play, so stopping is one load
        /// rather than a visible flash through an empty world.
        void RequestUnload(std::string returnScene = {});

        /// Stops delivering input to the running game without stopping it. See
        /// DotNetRuntime::SetInputEnabled — this is the editor's "eject" switch.
        void SetGameInputEnabled(bool enabled);

        /// Confines the running game's UI to a rectangle in ImGui screen coordinates: the game
        /// lays out against `width`/`height`, draws offset into the rect, and is clipped to it.
        /// An editor hosting the game inside a viewport panel passes that panel's rect; a host
        /// that owns the whole window never calls this. See FUiCanvas.
        void SetUiCanvas(float offsetX, float offsetY, float width, float height);
        void ClearUiCanvas();

        /// Rebuilds a game from its C# sources and publishes it where the next load will find it.
        /// If that game is the one currently running and it has hot reload on, the new assembly is
        /// picked up within the poll interval; otherwise it takes effect at the next load.
        ///
        /// Synchronous, and a publish takes seconds: the caller is expected to have drawn a frame
        /// saying so first. Returns false with outError set on any failure, having changed nothing.
        bool RebuildGame(const FManagedGameManifest& manifest, std::string& outError) const;

        EGameSessionState GetState() const { return state_; }
        bool IsPlaying() const { return state_ == EGameSessionState::Playing; }
        const FManagedGameManifest* GetActiveManifest() const;

        /// Set when a load fails, so a menu can say why instead of silently doing nothing.
        const std::string& GetLastError() const { return lastError_; }

        /// True once the managed side has repeatedly failed to collect an unloaded game. The host
        /// should stop offering to load anything and tell the user to restart.
        bool IsLeaking() const;

        /// Consecutive unloads whose load context survived. Zero is the healthy value and the one
        /// a stress test asserts: a process that swaps games all day must return to zero every
        /// time, because a streak is precisely how a managed leak announces itself.
        uint32_t UnloadPendingStreak() const;

        /// A game called Engine.RequestClose(). Returning true means the session handled it and the
        /// process must stay alive; false lets the engine close as it always has.
        void SetCloseRequestHandler(std::function<bool()> handler);
        bool HandleGameCloseRequest();

        // --- hooks forwarded by whoever owns the NextGameInstanceBase ---------------------------

        void OnHostInit();
        void OnHostDestroy();
        void OnHostTick(double deltaSeconds);
        bool OnRenderUI();
        void OnBeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                  std::vector<Assets::Model>& models,
                                  std::vector<Assets::FMaterial>& materials,
                                  std::vector<Assets::LightObject>& lights,
                                  std::vector<Assets::AnimationTrack>& tracks);
        void OnSceneLoaded();
        bool TryGetOverrideCamera(Assets::Camera& outCamera) const;
        void SetGamepadInput(int16_t leftStickX,
                             int16_t leftStickY,
                             int16_t rightStickX,
                             int16_t rightStickY,
                             int16_t leftTrigger,
                             int16_t rightTrigger);

    private:
        /// The world state a session is responsible for putting back. Anything a game can change
        /// that outlives the game itself belongs here; see the design's section 4.4 table.
        struct FWorldBaseline
        {
            Runtime::Config::ShowFlags showFlags;
            Runtime::Config::UserSettings userSettings;
            /// Every registered cvar's value at capture time. Recorded in full rather than as a
            /// diff: a game can change a cvar that already differed from its compiled default, and
            /// "reset to default" would then restore the wrong value.
            std::vector<std::pair<std::string, std::string>> cvarValues;
            std::string windowTitle;
            bool captured = false;
        };

        DotNetRuntime* GetRuntime() const;

        void PerformLoad(FManagedGameManifest manifest);
        /// resetScene rebuilds a world so the next game (or the editor) starts from something
        /// known. Skipped during engine shutdown, where queueing a scene load would never run.
        void PerformUnload(bool resetScene, const std::string& returnScene = {});

        void CaptureBaseline();
        void RestoreBaseline();
        void ApplyManifest(const FManagedGameManifest& manifest);

        NextEngine& engine_;
        EGameSessionState state_ = EGameSessionState::Idle;
        std::optional<FManagedGameManifest> activeManifest_;
        std::vector<std::string> linkedModules_;
        std::vector<std::string> baselineExcludedCVars_;
        std::function<bool()> closeRequestHandler_;
        FWorldBaseline baseline_;
        std::string lastError_;
        bool hostInitialised_ = false;
    };
}
