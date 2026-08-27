#pragma once

// Shared only by concrete renderer implementations in this module.

#include <memory>

#include <vulkan/vulkan.h>

namespace Assets
{
    class Scene;
    struct GPUScene;
}

namespace Vulkan
{
    class SwapChain;
    class VulkanBaseRenderer;
}

namespace Vulkan::PipelineCommon
{
    class ZeroBindPipeline;

    // Core.SurfaceBuild driver, shared by every renderer on the Primary Surface path.
    //
    // The pass is always full-rate: checkerboard is a shading allocation strategy and never applies
    // to surface data. Callers run it before GTAO, before any screen-space consumer, and before Core
    // Shading; that ordering is the point of the refactor, since it is what lets those consumers
    // read a dense depth/normal buffer instead of re-decoding the visibility buffer per step.
    class SurfaceBuildPass final
    {
    public:
        void CreateSwapChain(const SwapChain& swapChain, const Assets::Scene& scene);
        void DeleteSwapChain();

        // Records transitions + the full-rate build dispatch. gpuScene supplies the view bank base
        // and the surface path flag; CustomData1 is overwritten with the build's own output mask.
        // writeSpecularAlbedo is for consumers that actually read RT_SPECULAR_ALBEDO (the tracing
        // renderers); a light consumer should not pay the write.
        void Run(VulkanBaseRenderer& baseRender,
                 VkCommandBuffer commandBuffer,
                 const Assets::GPUScene& gpuScene,
                 bool writeSpecularAlbedo);

        bool IsValid() const { return pipeline_ != nullptr; }

    private:
        std::unique_ptr<ZeroBindPipeline> pipeline_;
    };
}
