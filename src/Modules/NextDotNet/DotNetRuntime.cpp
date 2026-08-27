#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Engine/Runtime/Profiling/TracyIntegration.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Platform/PlatformCommon.hpp"
#include "Engine/Utilities/FileHelper.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <cstdlib>

namespace Modules::NextDotNet
{
    namespace
    {
        constexpr double kHotReloadPollSeconds = 0.5;

        GkStr MakeStr(const std::string& value)
        {
            return GkStr{value.c_str(), static_cast<int32_t>(value.size())};
        }

        /// Where the managed assemblies live at runtime. CMake publishes them next to the
        /// executable so a built tree is self-contained; GK_DOTNET_MANAGED_DIR lets a developer
        /// point at a different publish without rebuilding.
        ///
        /// Deliberately relative to the executable rather than to the asset root: these are
        /// binaries that must sit beside the host, not content that can live inside a .pak.
        std::filesystem::path ResolveManagedRoot()
        {
            return DotNetRuntime::ManagedRoot();
        }

        std::filesystem::path ResolveDotNetRoot()
        {
            if (const char* fromEnv = std::getenv("DOTNET_ROOT"); fromEnv != nullptr && *fromEnv != '\0')
            {
                return std::filesystem::path(fromEnv);
            }
#if defined(GK_DOTNET_ROOT)
            return std::filesystem::path(GK_DOTNET_ROOT);
#else
            return {};
#endif
        }

        /// The C# sources, for the two operations that compile rather than load: the start-up
        /// rebuild and a host-initiated republish.
        ///
        /// Not resolved through the asset path: assets/csharp is not copied next to the executable
        /// — only its published output is — so resolving it against the runtime root silently finds
        /// nothing, which is how the start-up rebuild used to no-op in a normal build tree. The
        /// baked source path is the only thing that knows where the tree is; an installed build has
        /// no sources and correctly gets an empty path.
        std::filesystem::path ResolveManagedSourceRoot()
        {
            if (const char* fromEnv = std::getenv("GK_DOTNET_MANAGED_SOURCES");
                fromEnv != nullptr && *fromEnv != '\0')
            {
                return std::filesystem::path(fromEnv);
            }
#if defined(GK_DOTNET_MANAGED_SOURCE_ROOT)
            std::error_code ec;
            const std::filesystem::path baked(GK_DOTNET_MANAGED_SOURCE_ROOT);
            if (std::filesystem::exists(baked, ec))
            {
                return baked;
            }
#endif
            return {};
        }
    }

    std::filesystem::path DotNetRuntime::ManagedRoot()
    {
        if (const char* fromEnv = std::getenv("GK_DOTNET_MANAGED_DIR"); fromEnv != nullptr && *fromEnv != '\0')
        {
            return std::filesystem::path(fromEnv);
        }
        return NextRenderer::GetExecutableDirectory() / "csharp";
    }

    bool DotNetRuntime::PublishProject(const std::string& projectRelativeToManagedSources,
                                       const std::string& outputSubdirectory,
                                       std::string& outError)
    {
#if GK_DOTNET_USE_AOT
        (void)projectRelativeToManagedSources;
        (void)outputSubdirectory;
        outError = "the managed game is linked into this binary; rebuild the executable instead";
        return false;
#else
        const std::filesystem::path sourceRoot = ResolveManagedSourceRoot();
        if (sourceRoot.empty())
        {
            outError = "no C# sources are reachable from this build";
            return false;
        }

        const std::filesystem::path project = sourceRoot / projectRelativeToManagedSources;
        std::error_code ec;
        if (!std::filesystem::exists(project, ec))
        {
            outError = "project not found: " + project.string();
            return false;
        }

        // Same command CMake's gk_dotnet_managed_game runs. Deliberately the same output layout
        // too: a rebuild from inside the launcher must land where the next load will look, or it
        // would appear to succeed and change nothing.
        const std::string command = "dotnet publish \"" + project.string() + "\" -c Release -o \"" +
                                    (ManagedRoot() / outputSubdirectory).string() + "\" --nologo";
        SPDLOG_INFO("[dotnet] republishing {}", project.string());

        const int exitCode = NextRenderer::OSProcess(command.c_str());
        if (exitCode != 0)
        {
            outError = "dotnet publish failed (exit " + std::to_string(exitCode) + ")";
            return false;
        }
        return true;
#endif
    }

    DotNetRuntime::DotNetRuntime(NextEngine& engine, FConfig config)
        : engine_(engine)
        , config_(std::move(config))
    {
    }

    DotNetRuntime::~DotNetRuntime()
    {
        if (managed_ != nullptr)
        {
            CallLifecycleHook(EScriptHook::OnDestroy);
            UnloadGame();
        }
        GSceneBuildContext.Clear();
        GInputState.Reset();
    }

    void DotNetRuntime::Initialize()
    {
        if (config_.compileManagedSources)
        {
            EnsureManagedBuild();
        }

        FHostConfig hostConfig;
        hostConfig.managedRootDir = ResolveManagedRoot().string();
        hostConfig.dotnetRoot = ResolveDotNetRoot().string();

        host_ = CreateManagedHost(hostConfig);

        engineApi_ = BuildEngineApi();

        std::string error;
        if (!host_->Initialize(engineApi_, error))
        {
            SPDLOG_ERROR("[dotnet] managed host failed to start: {}", error);
            host_.reset();
            return;
        }

        managed_ = host_->Managed();
        if (managed_ == nullptr)
        {
            SPDLOG_ERROR("[dotnet] managed host reported no API table");
            host_.reset();
            return;
        }

        // Whether hot reload actually runs is decided per game at load time; what the host reports
        // here is whether the backend can do it at all.
        const char* hotReloadStatus = host_->SupportsHotReload() ? "available" : "unavailable";
        SPDLOG_INFO("[dotnet] {} host ready ({} bindings, hot reload {})",
                    host_->BackendName(),
                    GK_ENGINE_API_COUNT,
                    hotReloadStatus);

        if (config_.gameAssembly.empty())
        {
            // A valid state: the host is up and a game can be loaded later. This is what
            // gkNextLauncher starts in, and what a per-game host uses when its ManagedGameSession
            // owns the load.
            SPDLOG_INFO("[dotnet] no game assembly configured; runtime is idle");
            return;
        }

        LoadGameAssembly(config_.gameAssembly, config_.enableHotReload);
    }

    bool DotNetRuntime::SupportsRuntimeGameSwitching() const
    {
        return host_ != nullptr && host_->LoadsGameFromDisk();
    }

    bool DotNetRuntime::LoadGameAssembly(const std::string& relativeAssembly, bool enableHotReload)
    {
        if (managed_ == nullptr)
        {
            SPDLOG_ERROR("[dotnet] cannot load {}: the managed host is not running", relativeAssembly);
            return false;
        }

        if (!host_->LoadsGameFromDisk())
        {
            // NativeAOT: the game is linked in and GameHost.Load ignores the path. Loading it a
            // second time would re-run Initialize on the same module, so the linked-in game is
            // treated as loaded once and never replaced.
            if (gameLoaded_)
            {
                SPDLOG_WARN("[dotnet] ignoring load of {}: this backend has one linked-in game",
                            relativeAssembly);
                return false;
            }
            const int32_t status = managed_->LoadGame(MakeStr(relativeAssembly));
            if (status != static_cast<int32_t>(EGameStatus::Ok))
            {
                SPDLOG_ERROR("[dotnet] failed to load the linked-in game (status {})", status);
                return false;
            }
            gameLoaded_ = true;
            SPDLOG_INFO("[dotnet] game module is linked into the executable");
            return true;
        }

        if (gameLoaded_ && !UnloadGame())
        {
            return false;
        }

        const std::filesystem::path resolved = ResolveGameAssemblyPath(relativeAssembly);
        const std::string path = resolved.string();

        std::error_code ec;
        if (!std::filesystem::exists(resolved, ec))
        {
            SPDLOG_ERROR("[dotnet] game assembly not found: {}", path);
            return false;
        }

        const int32_t status = managed_->LoadGame(MakeStr(path));
        if (status != static_cast<int32_t>(EGameStatus::Ok))
        {
            SPDLOG_ERROR("[dotnet] failed to load {} (status {})", path, status);
            return false;
        }

        gameAssemblyPath_ = resolved;
        gameAssemblyTimestamp_ = std::filesystem::last_write_time(resolved, ec);
        hotReloadEnabled_ = enableHotReload;
        hotReloadElapsed_ = 0.0;
        gameLoaded_ = true;
        SPDLOG_INFO("[dotnet] loaded {}", path);
        return true;
    }

    bool DotNetRuntime::UnloadGame()
    {
        if (managed_ == nullptr || !gameLoaded_)
        {
            return true;
        }

        const int32_t status = managed_->UnloadGame();
        gameLoaded_ = false;
        gameAssemblyPath_.clear();
        isPaused_ = false;

        switch (static_cast<EGameStatus>(status))
        {
        case EGameStatus::Ok:
            unloadPendingStreak_ = 0;
            return true;
        case EGameStatus::UnloadPending:
            // The game is gone as far as the engine is concerned, but its load context is still
            // referenced. Tolerated once; the streak is what a host watches.
            ++unloadPendingStreak_;
            SPDLOG_WARN("[dotnet] unloaded the game, but its load context was not collected ({} in a row)",
                        unloadPendingStreak_);
            return true;
        default:
            SPDLOG_ERROR("[dotnet] unload failed (status {})", status);
            return false;
        }
    }

    void DotNetRuntime::Tick(double deltaSeconds)
    {
        // OnRenderUI normally clears edges after both managed consumers have seen them. If that
        // hook was suppressed for a UI-free screenshot, expire the previous frame here without
        // erasing any input event that already arrived for the current frame.
        GInputState.DiscardPressedBefore(engine_.GetTotalFrames());
        if (managed_ != nullptr && !isPaused_)
        {
            managed_->Tick(deltaSeconds);
        }

        TickHotReload(deltaSeconds);
    }

    void DotNetRuntime::SetPaused(bool paused)
    {
        if (isPaused_ == paused)
        {
            return;
        }
        isPaused_ = paused;
        if (paused)
        {
            GInputState.Reset();
        }
        SPDLOG_INFO("[dotnet] game tick {}", paused ? "paused" : "resumed");
    }

    void DotNetRuntime::SetInputEnabled(bool enabled)
    {
        if (inputEnabled_ == enabled)
        {
            return;
        }
        inputEnabled_ = enabled;
        if (!enabled)
        {
            // Whatever was held when input was cut never gets its key-up, so release it here.
            // Otherwise a game ejected out of mid-stride keeps walking for as long as it runs.
            GInputState.Reset();
        }
    }

    void DotNetRuntime::HandleEvent(const SDL_Event& event)
    {
        if (!inputEnabled_)
        {
            return;
        }

        FInputEvent forwarded{};
        bool forward = false;
        const uint32_t eventFrame = engine_.GetTotalFrames();

        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
            if (!event.key.repeat)
            {
                GInputState.BeginPressedFrame(eventFrame);
                GInputState.keysPressed.insert(event.key.key);
            }
            GInputState.keysDown.insert(event.key.key);
            forwarded.Type = static_cast<int32_t>(EInputEventType::KeyDown);
            forwarded.KeyCode = static_cast<int32_t>(event.key.key);
            forwarded.Repeated = event.key.repeat ? 1 : 0;
            forward = true;
            break;

        case SDL_EVENT_KEY_UP:
            GInputState.keysDown.erase(event.key.key);
            forwarded.Type = static_cast<int32_t>(EInputEventType::KeyUp);
            forwarded.KeyCode = static_cast<int32_t>(event.key.key);
            forward = true;
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            GInputState.BeginPressedFrame(eventFrame);
            GInputState.mouseButtonsPressed.insert(event.button.button);
            GInputState.mouseButtonsDown.insert(event.button.button);
            forwarded.Type = static_cast<int32_t>(EInputEventType::MouseButtonDown);
            forwarded.MouseButton = event.button.button;
            forward = true;
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            GInputState.mouseButtonsDown.erase(event.button.button);
            forwarded.Type = static_cast<int32_t>(EInputEventType::MouseButtonUp);
            forwarded.MouseButton = event.button.button;
            forward = true;
            break;

        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            GInputState.BeginPressedFrame(eventFrame);
            GInputState.gamepadButtonsPressed.insert(event.gbutton.button);
            GInputState.gamepadButtonsDown.insert(event.gbutton.button);
            forwarded.Type = static_cast<int32_t>(EInputEventType::GamepadButtonDown);
            forwarded.GamepadButton = event.gbutton.button;
            forward = true;
            break;

        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            GInputState.gamepadButtonsDown.erase(event.gbutton.button);
            forwarded.Type = static_cast<int32_t>(EInputEventType::GamepadButtonUp);
            forwarded.GamepadButton = event.gbutton.button;
            forward = true;
            break;

        default:
            break;
        }

        if (forward && managed_ != nullptr)
        {
            managed_->InputEvent(&forwarded);
        }
    }

    bool DotNetRuntime::CallLifecycleHook(EScriptHook hook, double deltaSeconds)
    {
        if (managed_ == nullptr)
        {
            return false;
        }
        const bool handled = managed_->Lifecycle(static_cast<int32_t>(hook), deltaSeconds) != 0;
        if (hook == EScriptHook::OnRenderUI)
        {
            // UI is the last managed consumer in a rendered frame. Keeping edges alive through
            // this hook lets C#-owned widgets observe the same click that gameplay saw in Tick;
            // clearing in Tick made every managed DrawList button miss its press.
            GInputState.ClearPressed();
        }
        return handled;
    }

    bool DotNetRuntime::CallBeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                               std::vector<Assets::Model>& models,
                                               std::vector<Assets::FMaterial>& materials,
                                               std::vector<Assets::LightObject>& lights,
                                               std::vector<Assets::AnimationTrack>& tracks)
    {
        if (managed_ == nullptr)
        {
            return false;
        }

        // The SceneBuild bindings are only usable while this context is set; anything the script
        // calls outside the hook is refused rather than silently writing into a stale vector.
        GSceneBuildContext.nodes = &nodes;
        GSceneBuildContext.models = &models;
        GSceneBuildContext.materials = &materials;
        GSceneBuildContext.lights = &lights;
        GSceneBuildContext.tracks = &tracks;

        const bool consumed = CallLifecycleHook(EScriptHook::BeforeSceneRebuild);

        GSceneBuildContext.Clear();
        return consumed;
    }

    bool DotNetRuntime::TryGetOverrideCamera(Assets::Camera& outCamera) const
    {
        if (managed_ == nullptr)
        {
            return false;
        }

        FCameraOverride camera{};
        camera.Up = FVec3{0.0f, 1.0f, 0.0f};
        camera.FieldOfView = 50.0f;
        if (managed_->OverrideCamera(&camera) == 0)
        {
            return false;
        }

        const glm::vec3 position(camera.Position.X, camera.Position.Y, camera.Position.Z);
        const glm::vec3 target(camera.Target.X, camera.Target.Y, camera.Target.Z);
        const glm::vec3 up(camera.Up.X, camera.Up.Y, camera.Up.Z);

        outCamera.ModelView = glm::lookAt(position, target, up);
        outCamera.FieldOfView = camera.FieldOfView;
        return true;
    }

    void DotNetRuntime::SetGamepadInput(int16_t leftStickX,
                                        int16_t leftStickY,
                                        int16_t rightStickX,
                                        int16_t rightStickY,
                                        int16_t leftTrigger,
                                        int16_t rightTrigger)
    {
        GInputState.gamepadAxes = {
            leftStickX,
            leftStickY,
            rightStickX,
            rightStickY,
            leftTrigger,
            rightTrigger,
        };
    }

    const char* DotNetRuntime::BackendName() const
    {
        return host_ != nullptr ? host_->BackendName() : "none";
    }

    std::filesystem::path DotNetRuntime::ResolveGameAssemblyPath(const std::string& relativeAssembly) const
    {
        if (relativeAssembly.empty())
        {
            return {};
        }
        const std::filesystem::path configured(relativeAssembly);
        if (configured.is_absolute())
        {
            return configured;
        }
        return ResolveManagedRoot() / configured;
    }

    bool DotNetRuntime::EnsureManagedBuild()
    {
        // Under NativeAOT the managed code is already inside this binary; there is nothing to
        // rebuild, and running the SDK would be misleading.
#if GK_DOTNET_USE_AOT
        return true;
#else
        // Deliberately shallow: the authoritative build happens in CMake, which publishes into the
        // binary directory. This only exists so editing C# and pressing run picks the change up
        // without a C++ rebuild, and it stays quiet when the SDK is not reachable.
        const std::filesystem::path sourceRoot = ResolveManagedSourceRoot();
        std::error_code ec;
        if (sourceRoot.empty())
        {
            return true;
        }

        std::filesystem::file_time_type newest{};
        bool found = false;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot, ec))
        {
            if (ec)
            {
                break;
            }
            if (!entry.is_regular_file(ec))
            {
                continue;
            }
            const std::filesystem::path& path = entry.path();
            if (path.extension() != ".cs" && path.extension() != ".csproj" && path.extension() != ".props")
            {
                continue;
            }
            // obj/ and bin/ hold build output whose timestamps would make this always dirty.
            const std::string text = path.string();
            if (text.find("\\obj\\") != std::string::npos || text.find("/obj/") != std::string::npos ||
                text.find("\\bin\\") != std::string::npos || text.find("/bin/") != std::string::npos)
            {
                continue;
            }
            const auto timestamp = entry.last_write_time(ec);
            if (!ec && (!found || timestamp > newest))
            {
                newest = timestamp;
                found = true;
            }
        }

        if (!found)
        {
            return true;
        }

        const std::filesystem::path stampPath = ResolveManagedRoot() / ".dotnet.stamp";
        if (std::filesystem::exists(stampPath, ec))
        {
            const auto stamp = std::filesystem::last_write_time(stampPath, ec);
            if (!ec && stamp >= newest)
            {
                return true;
            }
        }

        SPDLOG_INFO("[dotnet] managed sources changed; rebuilding assets/csharp");
        const std::string command = "dotnet publish \"" + (sourceRoot / "GkNext.Game" / "GkNext.Game.csproj").string() +
                                    "\" -c Release -o \"" + (ResolveManagedRoot() / "game").string() + "\" --nologo";
        const int exitCode = NextRenderer::OSProcess(command.c_str());
        if (exitCode != 0)
        {
            SPDLOG_WARN("[dotnet] managed rebuild failed (exit {}); using the previously published assemblies",
                        exitCode);
            return false;
        }

        std::ofstream(stampPath, std::ios::binary | std::ios::trunc).put('\n');
        return true;
#endif
    }

    void DotNetRuntime::TickHotReload(double deltaSeconds)
    {
        if (managed_ == nullptr || !hotReloadEnabled_ || host_ == nullptr || !host_->SupportsHotReload())
        {
            return;
        }
        if (!gameLoaded_ || gameAssemblyPath_.empty())
        {
            return;
        }

        hotReloadElapsed_ += deltaSeconds;
        if (hotReloadElapsed_ < kHotReloadPollSeconds)
        {
            return;
        }
        hotReloadElapsed_ = 0.0;

        std::error_code ec;
        const auto timestamp = std::filesystem::last_write_time(gameAssemblyPath_, ec);
        if (ec || timestamp == gameAssemblyTimestamp_)
        {
            return;
        }
        gameAssemblyTimestamp_ = timestamp;

        const std::string path = gameAssemblyPath_.string();
        const int32_t status = managed_->ReloadGame(MakeStr(path));
        switch (static_cast<EGameStatus>(status))
        {
        case EGameStatus::Ok:
            GkProfiling::Message(fmt::format("script hot reload: {}", path));
            SPDLOG_INFO("[dotnet] hot reloaded {}", path);
            break;
        case EGameStatus::UnloadPending:
            // The new code is live, but the previous load context is still referenced. Repeated
            // occurrences mean a leak that would eventually exhaust memory.
            GkProfiling::Message(fmt::format("script hot reload pending unload: {}", path));
            SPDLOG_WARN("[dotnet] hot reloaded {}, but the previous load context was not collected", path);
            break;
        default:
            SPDLOG_ERROR("[dotnet] hot reload of {} failed (status {})", path, status);
            break;
        }
    }
}
