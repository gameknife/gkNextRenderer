#include "Modules/NextDotNet/ManagedGameHostInstance.hpp"
#include "Modules/ScadLoader/ScadModule.hpp"

/// Brotato3D, written entirely in C#. Everything this executable adds over the shared managed host
/// is the SCAD loader its arenas need and the manifest that names the game.
std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                        Runtime::Config::Options& options,
                                                        NextEngine* engine)
{
    Modules::Scad::Register();

    return std::make_unique<Modules::NextDotNet::ManagedGameHostInstance>(
        config, options, engine,
        Modules::NextDotNet::FManagedGameHostOptions{
            .manifestPath = "assets/configs/games/brotato3d.game.json",
            .linkedModules = {"ScadLoader", "NextPhysics", "NextAudio", "GltfLoader"},
        });
}
