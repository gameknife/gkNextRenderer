#include "gkNextRenderer.hpp"
#include "Runtime/Application.hpp"

std::unique_ptr<NextGameInstanceBase> CreateGameInstance(Vulkan::WindowConfig& config, Options& options, NextRendererApplication* engine)
{
    return std::make_unique<NextGameInstanceVoid>(config, options, engine);
}
