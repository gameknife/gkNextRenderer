#include "Modules/NextDotNet/ManagedGameHostInstance.hpp"

/// Minimal host for the managed scripting layer: no gameplay on purpose. This is the target that
/// proves the runtime works inside the real engine under both backends.
std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                        Runtime::Config::Options& options,
                                                        NextEngine* engine)
{
    return std::make_unique<Modules::NextDotNet::ManagedGameHostInstance>(
        config, options, engine,
        Modules::NextDotNet::FManagedGameHostOptions{
            .manifestPath = "assets/configs/games/sandbox.game.json",
            .linkedModules = {"NextAudio", "NextPhysics", "GltfLoader"},
        });
}
