#include "LauncherGameInstance.hpp"

#include "Modules/ScadLoader/ScadModule.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                        Runtime::Config::Options& options,
                                                        NextEngine* engine)
{
    // Loaders are registered for every game this host might run, not for the one it happens to
    // start with. Registration is cheap and idempotent; discovering a missing loader after a game
    // is already loaded is not.
    Modules::Scad::Register();

    return std::make_unique<LauncherGameInstance>(config, options, engine);
}
