#pragma once

namespace Runtime::Config
{
    class Options;
}

namespace Modules::NextFidelityFX
{
    // Installs the FidelityFX FSR 3.1 provider and its composable frame-generation
    // swapchain layer alongside any other registered upscaler providers.
    void Install(const Runtime::Config::Options& options);
}
