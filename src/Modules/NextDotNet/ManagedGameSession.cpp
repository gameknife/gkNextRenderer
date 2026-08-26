#include "Modules/NextDotNet/ManagedGameSession.hpp"

#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Subsystems/NextAudio.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Modules/NextDotNet/EngineApi.hpp"

#include <algorithm>

namespace Modules::NextDotNet
{
    namespace
    {
        /// Rebuilt after every unload. Gives the renderer a live, empty world rather than the
        /// previous game's leftovers, and running the load path is what makes physics drop its
        /// bodies (OnSceneDestroyed / OnSceneStarted live in the scene load sequence).
        constexpr const char* kNeutralScene = "Empty.proc";

        /// Three consecutive uncollected load contexts is no longer "the GC has not run yet".
        constexpr uint32_t kUnloadPendingLeakThreshold = 3;
    }

    ManagedGameSession::ManagedGameSession(NextEngine& engine)
        : engine_(engine)
    {
    }

    ManagedGameSession::~ManagedGameSession() = default;

    DotNetRuntime* ManagedGameSession::GetRuntime() const
    {
        return Get(engine_);
    }

    void ManagedGameSession::SetLinkedModules(std::vector<std::string> modules)
    {
        linkedModules_ = std::move(modules);
        std::sort(linkedModules_.begin(), linkedModules_.end());
    }

    void ManagedGameSession::SetBaselineExcludedCVars(std::vector<std::string> names)
    {
        baselineExcludedCVars_ = std::move(names);
        std::sort(baselineExcludedCVars_.begin(), baselineExcludedCVars_.end());
    }

    bool ManagedGameSession::AreRequirementsMet(const FManagedGameManifest& manifest, std::string* outMissing) const
    {
        // An empty set means the host did not declare what it links. Refusing everything then would
        // make the check useless noise for a per-game executable that only ever runs one game.
        if (linkedModules_.empty())
        {
            return true;
        }

        std::vector<std::string> missing;
        for (const std::string& required : manifest.requiredModules)
        {
            if (!std::binary_search(linkedModules_.begin(), linkedModules_.end(), required))
            {
                missing.push_back(required);
            }
        }

        if (missing.empty())
        {
            return true;
        }
        if (outMissing != nullptr)
        {
            std::string joined;
            for (const std::string& name : missing)
            {
                if (!joined.empty())
                {
                    joined += ", ";
                }
                joined += name;
            }
            *outMissing = std::move(joined);
        }
        return false;
    }

    bool ManagedGameSession::RebuildGame(const FManagedGameManifest& manifest, std::string& outError) const
    {
        if (manifest.project.empty())
        {
            outError = "'" + manifest.id + "' declares no project to rebuild from";
            return false;
        }

        // The publish output directory is the first segment of the assembly path, which is how
        // gk_dotnet_managed_game's DIR argument reaches runtime: "flappy/FlappyCSharp.dll" was
        // published into csharp/flappy. Deriving it keeps the manifest from repeating itself.
        const std::filesystem::path assembly(manifest.assembly);
        const std::string subdirectory = assembly.has_parent_path() ? assembly.parent_path().string() : std::string();
        if (subdirectory.empty())
        {
            outError = "'" + manifest.id + "' has no publish subdirectory in its assembly path";
            return false;
        }

        return DotNetRuntime::PublishProject(manifest.project, subdirectory, outError);
    }

    const FManagedGameManifest* ManagedGameSession::GetActiveManifest() const
    {
        return activeManifest_ ? &*activeManifest_ : nullptr;
    }

    bool ManagedGameSession::IsLeaking() const
    {
        return UnloadPendingStreak() >= kUnloadPendingLeakThreshold;
    }

    uint32_t ManagedGameSession::UnloadPendingStreak() const
    {
        const DotNetRuntime* runtime = GetRuntime();
        return runtime != nullptr ? runtime->UnloadPendingStreak() : 0;
    }

    void ManagedGameSession::SetCloseRequestHandler(std::function<bool()> handler)
    {
        closeRequestHandler_ = std::move(handler);
    }

    bool ManagedGameSession::HandleGameCloseRequest()
    {
        if (!closeRequestHandler_)
        {
            return false;
        }
        return closeRequestHandler_();
    }

    void ManagedGameSession::RequestLoad(FManagedGameManifest manifest)
    {
        if (state_ == EGameSessionState::Loading || state_ == EGameSessionState::Unloading)
        {
            SPDLOG_WARN("[game] ignoring load of '{}': a transition is already queued", manifest.id);
            return;
        }

        engine_.AddTickedTask(
            [this, manifest = std::move(manifest)](double) mutable -> bool
            {
                if (engine_.GetEngineStatus() != NextRenderer::EApplicationStatus::Running)
                {
                    // Not ready yet; leave the task queued for a later frame.
                    return false;
                }
                PerformLoad(std::move(manifest));
                return true;
            });
    }

    void ManagedGameSession::RequestUnload(std::string returnScene)
    {
        if (state_ != EGameSessionState::Playing)
        {
            return;
        }

        engine_.AddTickedTask(
            [this, returnScene = std::move(returnScene)](double) -> bool
            {
                if (engine_.GetEngineStatus() != NextRenderer::EApplicationStatus::Running)
                {
                    return false;
                }
                PerformUnload(true, returnScene);
                return true;
            });
    }

    void ManagedGameSession::SetGameInputEnabled(bool enabled)
    {
        if (DotNetRuntime* runtime = GetRuntime())
        {
            runtime->SetInputEnabled(enabled);
        }
    }

    void ManagedGameSession::SetUiCanvas(float offsetX, float offsetY, float width, float height)
    {
        GUiCanvas.offset = {offsetX, offsetY};
        GUiCanvas.size = {width, height};
    }

    void ManagedGameSession::ClearUiCanvas()
    {
        GUiCanvas.Clear();
    }

    void ManagedGameSession::PerformLoad(FManagedGameManifest manifest)
    {
        lastError_.clear();

        DotNetRuntime* runtime = GetRuntime();
        if (runtime == nullptr || !runtime->IsReady())
        {
            lastError_ = "the .NET runtime is not available";
            SPDLOG_ERROR("[game] cannot load '{}': {}", manifest.id, lastError_);
            return;
        }

        if (std::string missing; !AreRequirementsMet(manifest, &missing))
        {
            lastError_ = "this host was not built with: " + missing;
            SPDLOG_ERROR("[game] cannot load '{}': {}", manifest.id, lastError_);
            return;
        }

        if (state_ == EGameSessionState::Playing)
        {
            PerformUnload(false);
        }

        if (IsLeaking())
        {
            lastError_ = "previous games were not collected; restart the process";
            SPDLOG_ERROR("[game] refusing to load '{}': {}", manifest.id, lastError_);
            return;
        }

        state_ = EGameSessionState::Loading;
        CaptureBaseline();

        if (!runtime->LoadGameAssembly(manifest.assembly, manifest.hotReload))
        {
            lastError_ = "failed to load " + manifest.assembly;
            state_ = EGameSessionState::Idle;
            RestoreBaseline();
            return;
        }

        activeManifest_ = std::move(manifest);
        ApplyManifest(*activeManifest_);

        // OnInit before the scene request: a game builds its scene content in BeforeSceneRebuild,
        // and that hook fires during the load it is about to ask for.
        runtime->CallLifecycleHook(EScriptHook::OnInit);

        if (!activeManifest_->initialScene.empty())
        {
            engine_.RequestLoadScene({.filename = activeManifest_->initialScene});
        }

        state_ = EGameSessionState::Playing;
        SPDLOG_INFO("[game] '{}' is running ({})", activeManifest_->id, activeManifest_->assembly);
    }

    void ManagedGameSession::PerformUnload(bool resetScene, const std::string& returnScene)
    {
        if (state_ != EGameSessionState::Playing)
        {
            return;
        }

        state_ = EGameSessionState::Unloading;
        const std::string id = activeManifest_ ? activeManifest_->id : std::string("<unknown>");

        if (DotNetRuntime* runtime = GetRuntime())
        {
            runtime->CallLifecycleHook(EScriptHook::OnDestroy);
            runtime->UnloadGame();
            // Input muting and pausing are properties of the session that just ended.
            runtime->SetInputEnabled(true);
            runtime->SetPaused(false);
        }

        // Audio outlives the scene: nothing else stops a game's music once its code is gone.
        if (NextAudio* audio = engine_.GetAudio())
        {
            audio->StopMusic();
        }

        RestoreBaseline();
        ClearUiCanvas();
        activeManifest_.reset();
        state_ = EGameSessionState::Idle;

        if (resetScene)
        {
            engine_.RequestLoadScene({.filename = returnScene.empty() ? kNeutralScene : returnScene});
        }

        SPDLOG_INFO("[game] '{}' unloaded", id);
    }

    void ManagedGameSession::SetAdjustWindow(bool adjustSize, bool adjustTitle)
    {
        adjustWindowSize_ = adjustSize;
        adjustWindowTitle_ = adjustTitle;
    }

    void ManagedGameSession::SetPaused(bool paused)
    {
        if (auto* runtime = GetRuntime())
        {
            runtime->SetPaused(paused);
        }
    }

    bool ManagedGameSession::IsPaused() const
    {
        if (auto* runtime = GetRuntime())
        {
            return runtime->IsPaused();
        }
        return false;
    }

    void ManagedGameSession::CaptureBaseline()
    {
        // Captured once per host, not once per game: after a game has run and been restored, the
        // world is already back at the baseline, and re-capturing would only risk recording a
        // half-restored state as the new truth.
        if (baseline_.captured)
        {
            return;
        }

        baseline_.showFlags = engine_.GetShowFlags();
        baseline_.userSettings = engine_.GetUserSettings();
        if (adjustWindowTitle_)
        {
            baseline_.windowTitle = engine_.GetWindow().GetTitle();
        }
        if (adjustWindowSize_)
        {
            const VkExtent2D windowExtent = engine_.GetWindow().WindowSize();
            baseline_.windowWidth = windowExtent.width;
            baseline_.windowHeight = windowExtent.height;
        }

        NextCVar::FCVarSystem& cvars = engine_.GetCVarSystem();
        baseline_.cvarValues.clear();
        cvars.ForEach(
            [&](const NextCVar::FCVarInfo& info)
            {
                if (NextCVar::HasFlag(info.flags, NextCVar::ECVarFlags::ReadOnly))
                {
                    return;
                }
                if (std::binary_search(baselineExcludedCVars_.begin(), baselineExcludedCVars_.end(), info.name))
                {
                    return;
                }
                baseline_.cvarValues.emplace_back(info.name, cvars.GetValueString(info.name));
            });

        baseline_.captured = true;
    }

    void ManagedGameSession::RestoreBaseline()
    {
        if (!baseline_.captured)
        {
            return;
        }

        engine_.GetShowFlags() = baseline_.showFlags;
        engine_.GetUserSettings() = baseline_.userSettings;
        if (adjustWindowTitle_ && !baseline_.windowTitle.empty())
        {
            engine_.GetWindow().SetTitle(baseline_.windowTitle);
        }

        if (adjustWindowSize_ && baseline_.windowWidth > 0 && baseline_.windowHeight > 0)
        {
            engine_.GetWindow().SetSize(baseline_.windowWidth, baseline_.windowHeight);
            engine_.GetWindow().SetPositionCentered();
        }

        NextCVar::FCVarSystem& cvars = engine_.GetCVarSystem();
        for (const auto& [name, value] : baseline_.cvarValues)
        {
            if (cvars.GetValueString(name) == value)
            {
                continue;
            }
            std::string error;
            if (!cvars.SetValueFromString(name, value, NextCVar::ECVarSetBy::Console, &error))
            {
                SPDLOG_WARN("[game] could not restore cvar {} to '{}': {}", name, value, error);
            }
        }
    }

    void ManagedGameSession::ApplyManifest(const FManagedGameManifest& manifest)
    {
        Runtime::Config::ShowFlags& flags = engine_.GetShowFlags();
        if (manifest.showFlags.debugGraphicsPanel)
        {
            flags.DebugGraphicsPanel = *manifest.showFlags.debugGraphicsPanel;
        }
        if (manifest.showFlags.debugPhysicsOverlay)
        {
            flags.DebugPhysicsOverlay = *manifest.showFlags.debugPhysicsOverlay;
        }
        if (manifest.showFlags.overlay)
        {
            engine_.GetUserSettings().ShowOverlay = *manifest.showFlags.overlay;
        }

        if (adjustWindowTitle_ && !manifest.window.title.empty())
        {
            engine_.GetWindow().SetTitle(manifest.window.title);
        }

        if (adjustWindowSize_ && manifest.window.width > 0 && manifest.window.height > 0)
        {
            engine_.GetWindow().SetSize(manifest.window.width, manifest.window.height);
            engine_.GetWindow().SetPositionCentered();
        }
    }

    void ManagedGameSession::OnHostInit()
    {
        hostInitialised_ = true;
    }

    void ManagedGameSession::OnHostDestroy()
    {
        // The engine is going away: unload without queueing anything, because no further frame will
        // run the task that a scene reset would need.
        PerformUnload(false);
        hostInitialised_ = false;
    }

    void ManagedGameSession::OnHostTick(double)
    {
        // The script runtime is ticked by the engine itself; nothing to do per frame here yet.
        // Kept as a hook so a host does not have to change shape when the session grows one.
    }

    bool ManagedGameSession::OnRenderUI()
    {
        if (!IsPlaying())
        {
            return false;
        }
        DotNetRuntime* runtime = GetRuntime();
        return runtime != nullptr && runtime->CallLifecycleHook(EScriptHook::OnRenderUI);
    }

    void ManagedGameSession::OnBeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                                  std::vector<Assets::Model>& models,
                                                  std::vector<Assets::FMaterial>& materials,
                                                  std::vector<Assets::LightObject>& lights,
                                                  std::vector<Assets::AnimationTrack>& tracks)
    {
        if (!IsPlaying())
        {
            return;
        }
        if (DotNetRuntime* runtime = GetRuntime())
        {
            runtime->CallBeforeSceneRebuild(nodes, models, materials, lights, tracks);
        }
    }

    void ManagedGameSession::OnSceneLoaded()
    {
        if (!IsPlaying())
        {
            return;
        }
        if (DotNetRuntime* runtime = GetRuntime())
        {
            runtime->CallLifecycleHook(EScriptHook::OnSceneLoaded);
        }
    }

    bool ManagedGameSession::TryGetOverrideCamera(Assets::Camera& outCamera) const
    {
        if (!IsPlaying())
        {
            return false;
        }
        const DotNetRuntime* runtime = GetRuntime();
        return runtime != nullptr && runtime->TryGetOverrideCamera(outCamera);
    }

    void ManagedGameSession::SetGamepadInput(int16_t leftStickX,
                                             int16_t leftStickY,
                                             int16_t rightStickX,
                                             int16_t rightStickY,
                                             int16_t leftTrigger,
                                             int16_t rightTrigger)
    {
        if (DotNetRuntime* runtime = GetRuntime())
        {
            runtime->SetGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger, rightTrigger);
        }
    }
}
