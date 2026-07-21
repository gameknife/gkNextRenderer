#pragma once

namespace Runtime::Config
{
    class Options;
}

namespace Modules::NextFidelityFX
{
    // Installs the FidelityFX FSR 3.1 Vulkan provider when no higher-priority
    // upscaler provider has already claimed the process-wide Vulkan seams.
    void Install(const Runtime::Config::Options& options);
}
