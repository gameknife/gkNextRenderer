#pragma once

#include <cstdint>

namespace Vulkan::PipelineCommon
{
    constexpr uint32_t checkerboardShadingFlag = 1u << 0u;

    // Which lighting channels the resolve reconstructs for the parity this frame did not shade.
    //
    // Checkerboard rendering requires the Primary Surface path (VulkanBaseRenderer::
    // IsCheckerboardRenderingActive), because Core.SurfaceBuild is what makes every surface-class RT
    // dense at full rate. Reconstruction therefore only ever touches lighting; the sets that used to
    // neighbour-copy depth / normal / motion / object id are gone, along with checkerboard support
    // for the renderers that never got a Build pass.
    enum class ECheckerboardResolveSet : uint32_t
    {
        Tracing = 1,
        SoftwareModernNoAmbient = 2,
    };

    constexpr uint32_t GetCheckerboardDispatchWidth(const uint32_t width, const bool enabled)
    {
        return enabled ? (width + 1u) / 2u : width;
    }

    constexpr uint32_t GetCheckerboardPixelX(
        const uint32_t compactX,
        const uint32_t y,
        const uint32_t frameIndex)
    {
        return compactX * 2u + ((y + frameIndex) & 1u);
    }

    constexpr uint32_t GetCheckerboardMissingPixelX(
        const uint32_t compactX,
        const uint32_t y,
        const uint32_t frameIndex)
    {
        return compactX * 2u + (1u - ((y + frameIndex) & 1u));
    }
}
