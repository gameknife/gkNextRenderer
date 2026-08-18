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
            if (const char* fromEnv = std::getenv("GK_DOTNET_MANAGED_DIR"); fromEnv != nullptr && *fromEnv != '\0')
            {
                return std::filesystem::path(fromEnv);
            }
            return NextRenderer::GetExecutableDirectory() / "csharp";
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
            managed_->UnloadGame();
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

        SPDLOG_INFO("[dotnet] {} host ready ({} bindings, hot reload {})",
                    host_->BackendName(),
                    GK_ENGINE_API_COUNT,
                    host_->SupportsHotReload() ? "on" : "unavailable");

        gameAssemblyPath_ = ResolveGameAssemblyPath();
        if (config_.gameAssembly.empty())
        {
            SPDLOG_INFO("[dotnet] no game assembly configured; runtime is idle");
            return;
        }

        const std::string path = gameAssemblyPath_.string();
        const int32_t status = managed_->LoadGame(MakeStr(path));
        if (status != static_cast<int32_t>(EGameStatus::Ok))
        {
            SPDLOG_ERROR("[dotnet] failed to load {} (status {})", path, status);
            return;
        }

        if (host_->LoadsGameFromDisk())
        {
            std::error_code ec;
            gameAssemblyTimestamp_ = std::filesystem::last_write_time(gameAssemblyPath_, ec);
            SPDLOG_INFO("[dotnet] loaded {}", path);
        }
        else
        {
            SPDLOG_INFO("[dotnet] game module is linked into the executable");
        }
    }

    void DotNetRuntime::Tick(double deltaSeconds)
    {
        if (managed_ != nullptr)
        {
            managed_->Tick(deltaSeconds);
        }

        // Cleared after the script has seen the frame, so "pressed this frame" means the same thing
        // it did under QuickJS.
        GInputState.ClearPressed();

        TickHotReload(deltaSeconds);
    }

    void DotNetRuntime::HandleEvent(const SDL_Event& event)
    {
        FInputEvent forwarded{};
        bool forward = false;

        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
            if (!event.key.repeat)
            {
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
        return managed_->Lifecycle(static_cast<int32_t>(hook), deltaSeconds) != 0;
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

    const char* DotNetRuntime::BackendName() const
    {
        return host_ != nullptr ? host_->BackendName() : "none";
    }

    std::filesystem::path DotNetRuntime::ResolveGameAssemblyPath() const
    {
        if (config_.gameAssembly.empty())
        {
            return {};
        }
        const std::filesystem::path configured(config_.gameAssembly);
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
        const std::filesystem::path sourceRoot =
            std::filesystem::path(Utilities::FileHelper::GetNormalizedFilePath("assets/csharp"));
        std::error_code ec;
        if (!std::filesystem::exists(sourceRoot, ec))
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
        if (managed_ == nullptr || !config_.enableHotReload || host_ == nullptr || !host_->SupportsHotReload())
        {
            return;
        }
        if (gameAssemblyPath_.empty())
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
