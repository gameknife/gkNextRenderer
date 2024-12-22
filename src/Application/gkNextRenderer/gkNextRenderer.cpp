#include "gkNextRenderer.hpp"
#include "Runtime/Application.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextEngine* engine)
{
    return std::make_unique<NextGameInstanceVoid>(config, options, engine);
}
