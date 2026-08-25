#include "Modules/NextDotNet/ManagedGameHostInstance.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Modules/NextDotNet/DotNetRuntime.hpp"
#include "Modules/NextDotNet/NextDotNetModule.hpp"

namespace Modules::NextDotNet
{
    ManagedGameHostInstance::ManagedGameHostInstance(Vulkan::WindowConfig& config,
                                                     Runtime::Config::Options& options,
                                                     NextEngine* engine,
                                                     FManagedGameHostOptions hostOptions)
        : NextGameInstanceBase(config, options, engine)
        , hostOptions_(std::move(hostOptions))
        , session_(*engine)
    {
        FManagedGameManifest::FWindow window = hostOptions_.window;
        bool compileManagedSources = false;

        if (!hostOptions_.manifestPath.empty())
        {
            bootManifest_ = LoadManagedGameManifest(hostOptions_.manifestPath);
            if (bootManifest_)
            {
                window = bootManifest_->window;
                compileManagedSources = bootManifest_->compileManagedSources;
            }
            else
            {
                // Keep going with the fallback window: an engine that starts and says the manifest
                // is broken is more useful than one that dies before it can log anything.
                SPDLOG_ERROR("[game] starting without a game: {} could not be loaded",
                             hostOptions_.manifestPath);
            }
        }

        ConfigureWindow(config, options, window.title, window.width, window.height, window.forceSDR);

        // The runtime starts idle on purpose. Which assembly is loaded, and whether it hot reloads,
        // is the session's decision — the same decision whether this host runs one game forever or
        // swaps between several.
        Install(*engine,
                {
                    .gameAssembly = "",
                    .compileManagedSources = compileManagedSources,
                    .enableHotReload = false,
                });

        session_.SetLinkedModules(hostOptions_.linkedModules);
    }

    ManagedGameHostInstance::~ManagedGameHostInstance() = default;

    void ManagedGameHostInstance::OnInit()
    {
        session_.OnHostInit();

        if (bootManifest_)
        {
            session_.RequestLoad(*bootManifest_);
        }
    }

    void ManagedGameHostInstance::OnTick(double deltaSeconds)
    {
        session_.OnHostTick(deltaSeconds);
    }

    void ManagedGameHostInstance::OnDestroy()
    {
        session_.OnHostDestroy();
    }

    bool ManagedGameHostInstance::OnRenderUI()
    {
        const bool hostConsumed = OnHostRenderUI();
        const bool gameConsumed = session_.OnRenderUI();
        return hostConsumed || gameConsumed;
    }

    bool ManagedGameHostInstance::OnKey(SDL_Event& event)
    {
        return OnHostKey(event);
    }

    bool ManagedGameHostInstance::OnMouseButton(SDL_Event&)
    {
        return false;
    }

    bool ManagedGameHostInstance::OnGamepadInput(int16_t leftStickX,
                                                 int16_t leftStickY,
                                                 int16_t rightStickX,
                                                 int16_t rightStickY,
                                                 int16_t leftTrigger,
                                                 int16_t rightTrigger)
    {
        session_.SetGamepadInput(leftStickX, leftStickY, rightStickX, rightStickY, leftTrigger, rightTrigger);
        return false;
    }

    void ManagedGameHostInstance::BeforeSceneRebuild(std::vector<std::shared_ptr<Assets::Node>>& nodes,
                                                      std::vector<Assets::Model>& models,
                                                      std::vector<Assets::FMaterial>& materials,
                                                      std::vector<Assets::LightObject>& lights,
                                                      std::vector<Assets::AnimationTrack>& tracks)
    {
        session_.OnBeforeSceneRebuild(nodes, models, materials, lights, tracks);
    }

    void ManagedGameHostInstance::OnSceneLoaded()
    {
        session_.OnSceneLoaded();
    }

    bool ManagedGameHostInstance::OverrideRenderCamera(Assets::Camera& outRenderCamera) const
    {
        return session_.TryGetOverrideCamera(outRenderCamera);
    }
}
