#pragma once

namespace Runtime::Config
{
    class Options;
}

namespace Modules::NextStreamline
{
    // Initializes NVIDIA Streamline (DLSS-SR / Frame Generation / Reflex) when the
    // hardware supports it and hooks the engine's Vulkan interposer, device
    // augmenter and upscaler factory. Call from the application entry before the
    // engine is created; a no-op on non-Windows builds or when disabled.
    void Install(const Runtime::Config::Options& options);
}
