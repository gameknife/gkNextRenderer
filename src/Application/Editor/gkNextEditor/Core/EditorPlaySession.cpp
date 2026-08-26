#include "Core/EditorPlaySession.hpp"

#include "Engine/Runtime/Engine.hpp"

#if GK_MODULE_NEXTDOTNET
#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Modules/NextDotNet/ManagedGameManifest.hpp"
#include "Modules/NextDotNet/ManagedGameSession.hpp"
#include "Modules/NextDotNet/NextDotNetModule.hpp"
#endif

#include <algorithm>

namespace Editor
{
#if GK_MODULE_NEXTDOTNET

    namespace
    {
        using namespace Modules::NextDotNet;

        /// What the editor links, in the form manifests declare. gkNextEditor already carries the
        /// content loaders and the standard runtime; a game needing something else is refused in
        /// the menu rather than halfway through a load.
        const std::vector<std::string> kEditorLinkedModules = {
            "DevTools",       "GltfLoader",           "LDrawLoader",    "LiveCoding", "NextAudio",
            "NextCapture",    "NextDotNet",           "NextFidelityFX", "NextPhysics", "NextRemote",
            "NextStreamline", "NextTemporalUpscaler", "NextValidation", "NextUI",     "RenderViews",
            "ScadLoader",     "SceneContent",         "SceneExport",    "SplatLoader",
        };
    }

    struct FPlaySession::FImpl
    {
        explicit FImpl(NextEngine& owner) : engine(owner), session(owner)
        {
            // In editor Play-in-Editor mode, never resize the main editor window or change title;
            // games render directly inside the editor's embedded viewport.
            session.SetAdjustWindow(false, false);
        }

        NextEngine& engine;
        ManagedGameSession session;
        std::vector<FPlayGameEntry> games;
        std::vector<FManagedGameManifest> manifests;
        EPlayState state = EPlayState::Stopped;
        /// Whether the underlying session was running as of the previous tick. Transitions are
        /// queued as ticked tasks, so between "play" and the load actually happening the session
        /// still reads as Idle; comparing against the previous frame is what tells an unstarted
        /// session apart from one that has ended.
        bool sessionWasRunning = false;
        std::string returnScene;
        std::string unavailableReason;
        bool available = false;

        const FManagedGameManifest* FindManifest(const std::string& id) const
        {
            const auto it = std::find_if(manifests.begin(), manifests.end(),
                                         [&id](const FManagedGameManifest& m) { return m.id == id; });
            return it != manifests.end() ? &*it : nullptr;
        }

        void RefreshGames()
        {
            manifests = ScanManagedGameManifests(kManagedGameManifestDirectory);
            games.clear();
            games.reserve(manifests.size());

            for (const FManagedGameManifest& manifest : manifests)
            {
                FPlayGameEntry entry;
                entry.id = manifest.id;
                entry.displayName = manifest.displayName;
                entry.canRebuild = !manifest.project.empty();

                if (std::string missing; !session.AreRequirementsMet(manifest, &missing))
                {
                    entry.unavailableReason = "needs " + missing;
                }
                else
                {
                    const std::filesystem::path assemblyPath =
                        DotNetRuntime::ManagedRoot() / manifest.assembly;
                    std::error_code ec;
                    if (!std::filesystem::exists(assemblyPath, ec))
                    {
                        entry.unavailableReason = "not built (" + manifest.assembly + ")";
                    }
                }

                entry.available = entry.unavailableReason.empty();
                games.push_back(std::move(entry));
            }
        }
    };

    FPlaySession::FPlaySession(NextEngine& engine) : impl_(std::make_unique<FImpl>(engine))
    {
        // The runtime starts idle; which game runs is decided when the user presses Play.
        Modules::NextDotNet::Install(engine, {.gameAssembly = "", .compileManagedSources = false,
                                              .enableHotReload = false});
    }

    FPlaySession::~FPlaySession() = default;

    void FPlaySession::Initialize()
    {
        impl_->session.SetLinkedModules(kEditorLinkedModules);
        // The editor drives this session through these two cvars. They describe the editor, not
        // the world; restoring them on unload would restart the game the user just stopped.
        impl_->session.SetBaselineExcludedCVars({"ed.play", "ed.playEject"});

        DotNetRuntime* runtime = Get(impl_->engine);
        if (runtime == nullptr || !runtime->IsReady())
        {
            impl_->unavailableReason = "the .NET runtime failed to start; see the log";
        }
        else if (!runtime->SupportsRuntimeGameSwitching())
        {
            // NativeAOT links one game into the binary. Play-in-editor has nothing to choose.
            impl_->unavailableReason = "this build uses the NativeAOT backend, which links a single "
                                       "game into the executable";
        }
        else
        {
            impl_->available = true;
        }

        impl_->RefreshGames();

        impl_->session.SetCloseRequestHandler(
            [this]() -> bool
            {
                Stop();
                return true;
            });
    }

    bool FPlaySession::IsAvailable() const { return impl_->available; }
    const std::string& FPlaySession::UnavailableReason() const { return impl_->unavailableReason; }
    const std::vector<FPlayGameEntry>& FPlaySession::Games() const { return impl_->games; }
    EPlayState FPlaySession::State() const { return impl_->state; }

    std::string FPlaySession::ActiveGameId() const
    {
        const FManagedGameManifest* manifest = impl_->session.GetActiveManifest();
        return manifest != nullptr ? manifest->id : std::string();
    }

    std::string FPlaySession::LastError() const { return impl_->session.GetLastError(); }

    void FPlaySession::Play(const std::string& gameId, std::string returnScene)
    {
        if (!impl_->available || impl_->state != EPlayState::Stopped)
        {
            return;
        }

        const FManagedGameManifest* manifest = impl_->FindManifest(gameId);
        if (manifest == nullptr)
        {
            SPDLOG_ERROR("[pie] no managed game with id '{}'", gameId);
            return;
        }

        // Captured before the game replaces the world, because that is the whole of the "restore"
        // this design promises: reload this file when the session stops.
        impl_->returnScene = std::move(returnScene);

        // The game builds its own world, and an editor session leaves selection and undo pointing
        // at nodes that are about to stop existing.
        impl_->engine.GetScene().ClearSelection();
        impl_->engine.GetCommandHistory().Clear();

        impl_->state = EPlayState::Playing;
        impl_->session.SetGameInputEnabled(true);
        impl_->session.RequestLoad(*manifest);
        SPDLOG_INFO("[pie] playing '{}' (stop returns to '{}')", gameId,
                    impl_->returnScene.empty() ? "an empty scene" : impl_->returnScene);
    }

    void FPlaySession::Stop()
    {
        if (impl_->state == EPlayState::Stopped)
        {
            return;
        }

        impl_->state = EPlayState::Stopped;
        impl_->session.SetGameInputEnabled(true);
        impl_->engine.GetScene().ClearSelection();
        impl_->engine.GetCommandHistory().Clear();
        impl_->session.RequestUnload(impl_->returnScene);

        // A game may have been rebuilt while it was running, or built for the first time; either
        // way the menu's availability is now stale.
        impl_->RefreshGames();
        SPDLOG_INFO("[pie] stopped");
    }

    void FPlaySession::SetEjected(bool ejected)
    {
        if (impl_->state == EPlayState::Stopped)
        {
            return;
        }
        const EPlayState target = ejected ? EPlayState::Ejected : EPlayState::Playing;
        if (impl_->state == target)
        {
            return;
        }
        impl_->state = target;
        impl_->session.SetGameInputEnabled(!ejected);
        SPDLOG_INFO("[pie] {}", ejected ? "ejected; input and camera are the editor's" : "resumed");
    }

    void FPlaySession::ToggleEject() { SetEjected(impl_->state == EPlayState::Playing); }

    bool FPlaySession::IsPaused() const
    {
        return impl_->session.IsPaused();
    }

    void FPlaySession::SetPaused(bool paused)
    {
        if (impl_->state == EPlayState::Stopped)
        {
            return;
        }
        impl_->session.SetPaused(paused);
    }

    void FPlaySession::TogglePause()
    {
        SetPaused(!IsPaused());
    }

    bool FPlaySession::Rebuild(const std::string& gameId, std::string& outError)
    {
        const FManagedGameManifest* manifest = impl_->FindManifest(gameId);
        if (manifest == nullptr)
        {
            outError = "no managed game with id '" + gameId + "'";
            return false;
        }
        if (!impl_->session.RebuildGame(*manifest, outError))
        {
            return false;
        }
        impl_->RefreshGames();
        return true;
    }

    void FPlaySession::OnTick(double deltaSeconds)
    {
        impl_->session.OnHostTick(deltaSeconds);

        // The session can end without the editor asking — a load failure, or a game that quit
        // through a path the close handler did not cover. Keep the editor's view of the state
        // honest rather than leaving it stuck in Playing with nothing running.
        //
        // Only an observed running-to-idle transition counts. Testing "is the session idle?" on its
        // own would fire in the gap between pressing Play and the queued load running, cancelling
        // every Play one frame after it started.
        const bool sessionRunning =
            impl_->session.GetState() != Modules::NextDotNet::EGameSessionState::Idle;
        if (impl_->sessionWasRunning && !sessionRunning && impl_->state != EPlayState::Stopped)
        {
            impl_->state = EPlayState::Stopped;
        }
        impl_->sessionWasRunning = sessionRunning;
    }

    bool FPlaySession::OnRenderGameUI(float viewportX, float viewportY, float viewportWidth,
                                      float viewportHeight)
    {
        if (impl_->state != EPlayState::Playing)
        {
            return false;
        }

        impl_->session.SetUiCanvas(viewportX, viewportY, viewportWidth, viewportHeight);
        const bool consumed = impl_->session.OnRenderUI();
        // Cleared straight away rather than left set for the frame: everything else that reaches
        // these bindings — a script console, a future tool — is not the game and should see the
        // ordinary full-window coordinates.
        impl_->session.ClearUiCanvas();
        return consumed;
    }

    void FPlaySession::OnBeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                            std::vector<Assets::Model>& models,
                                            std::vector<Assets::FMaterial>& materials,
                                            std::vector<Assets::LightObject>& lights,
                                            std::vector<Assets::AnimationTrack>& tracks)
    {
        impl_->session.OnBeforeSceneRebuild(nodes, models, materials, lights, tracks);
    }

    void FPlaySession::OnSceneLoaded() { impl_->session.OnSceneLoaded(); }

    bool FPlaySession::TryGetOverrideCamera(Assets::Camera& outCamera) const
    {
        // Ejected means the editor is flying its own camera through the game's world.
        if (impl_->state != EPlayState::Playing)
        {
            return false;
        }
        return impl_->session.TryGetOverrideCamera(outCamera);
    }

    void FPlaySession::SetGamepadInput(int16_t leftStickX,
                                       int16_t leftStickY,
                                       int16_t rightStickX,
                                       int16_t rightStickY,
                                       int16_t leftTrigger,
                                       int16_t rightTrigger)
    {
        if (impl_->state != EPlayState::Playing)
        {
            return;
        }
        impl_->session.SetGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger,
                                       rightTrigger);
    }

    bool FPlaySession::OnGameRequestedClose()
    {
        if (impl_->state == EPlayState::Stopped)
        {
            return false;
        }
        Stop();
        return true;
    }

    void FPlaySession::OnEditorDestroy() { impl_->session.OnHostDestroy(); }

#else // GK_MODULE_NEXTDOTNET

    // No managed runtime in this build. Every entry point still exists so the editor needs no
    // conditional compilation of its own; they simply report why nothing can run.
    struct FPlaySession::FImpl
    {
        std::vector<FPlayGameEntry> games;
        std::string unavailableReason = "this build was made without the .NET scripting module";
        std::string empty;
    };

    FPlaySession::FPlaySession(NextEngine&) : impl_(std::make_unique<FImpl>()) {}
    FPlaySession::~FPlaySession() = default;
    void FPlaySession::Initialize() {}
    bool FPlaySession::IsAvailable() const { return false; }
    const std::string& FPlaySession::UnavailableReason() const { return impl_->unavailableReason; }
    const std::vector<FPlayGameEntry>& FPlaySession::Games() const { return impl_->games; }
    EPlayState FPlaySession::State() const { return EPlayState::Stopped; }
    std::string FPlaySession::ActiveGameId() const { return {}; }
    std::string FPlaySession::LastError() const { return {}; }
    void FPlaySession::Play(const std::string&, std::string) {}
    void FPlaySession::Stop() {}
    void FPlaySession::SetEjected(bool) {}
    void FPlaySession::ToggleEject() {}
    bool FPlaySession::Rebuild(const std::string&, std::string& outError)
    {
        outError = impl_->unavailableReason;
        return false;
    }
    void FPlaySession::OnTick(double) {}
    bool FPlaySession::OnRenderGameUI(float, float, float, float) { return false; }
    void FPlaySession::OnBeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>&,
                                            std::vector<Assets::Model>&,
                                            std::vector<Assets::FMaterial>&,
                                            std::vector<Assets::LightObject>&,
                                            std::vector<Assets::AnimationTrack>&)
    {
    }
    void FPlaySession::OnSceneLoaded() {}
    bool FPlaySession::TryGetOverrideCamera(Assets::Camera&) const { return false; }
    void FPlaySession::SetGamepadInput(int16_t, int16_t, int16_t, int16_t, int16_t, int16_t) {}
    bool FPlaySession::OnGameRequestedClose() { return false; }
    void FPlaySession::OnEditorDestroy() {}

#endif // GK_MODULE_NEXTDOTNET
}
