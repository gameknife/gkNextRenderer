#pragma once

namespace Vulkan
{
    // Registers the renderer implementations owned by gkNextEngine.
    // Idempotent so multiple engine fixtures may be created in one process.
    void RegisterBuiltinRendererProviders();
}
