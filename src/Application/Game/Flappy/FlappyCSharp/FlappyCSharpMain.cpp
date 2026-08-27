#include "Modules/NextDotNet/ManagedGameHostInstance.hpp"

/// The C# half of the Flappy parity pair. FlappyCpp is the same game written in C++; the two are
/// expected to produce identical replays, which is what makes this target the binding layer's
/// regression net (docs/projects/flappy-bird-parity/introduction.md).
std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                        Runtime::Config::Options& options,
                                                        NextEngine* engine)
{
    return std::make_unique<Modules::NextDotNet::ManagedGameHostInstance>(
        config, options, engine,
        Modules::NextDotNet::FManagedGameHostOptions{
            .manifestPath = "assets/configs/games/flappy.game.json",
            .linkedModules = {"NextAudio", "NextPhysics", "GltfLoader"},
        });
}
