#include "LauncherGameInstance.hpp"

#include "Gameplay/Rig/RigSubsystem.h"
#include "Modules/ScadLoader/ScadModule.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config,
                                                        Runtime::Config::Options& options,
                                                        NextEngine* engine)
{
    // Loaders are registered for every game this host might run, not for the one it happens to
    // start with. Registration is cheap and idempotent; discovering a missing loader after a game
    // is already loaded is not.
    Modules::Scad::Register();

    // ScadRig characters, for the games that have them. Installed for the same reason the loaders
    // are: which game runs is not known yet, and a subsystem cannot be acquired after the fact.
    if (engine != nullptr)
    {
        NextGameplay::Rig::Install(*engine);
    }

    return std::make_unique<LauncherGameInstance>(config, options, engine);
}
