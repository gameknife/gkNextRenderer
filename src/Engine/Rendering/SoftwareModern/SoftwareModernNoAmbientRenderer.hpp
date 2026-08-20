#pragma once

// Core renderer implementation owned by gkNextEngine.

#include "Engine/Rendering/PipelineCommon/ShadingSchedulerPass.hpp"
#include "Engine/Rendering/PipelineCommon/SurfaceBuildPass.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include <memory>

namespace Vulkan
{
    namespace PipelineCommon
    {
        class ZeroBindPipeline;
    }
}

namespace Vulkan::SoftwareModernNoAmbient
{
    class SoftwareModernNoAmbientRenderer final : public Vulkan::LogicRendererBase
    {
    public:
        VULKAN_NON_COPIABLE(SoftwareModernNoAmbientRenderer)

        explicit SoftwareModernNoAmbientRenderer(Vulkan::VulkanBaseRenderer& baseRender);
        ~SoftwareModernNoAmbientRenderer();

        void CreateSwapChain(const VkExtent2D& extent) override;
        void DeleteSwapChain() override;
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

    private:
        void ShadeSurface(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene);
        void Compose(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene);
        void DispatchGTAO(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        // Primary Surface path: build once, then lighting-only kernels.
        PipelineCommon::SurfaceBuildPass surfaceBuild_;
        // Tile classification + one indirect dispatch per shading bucket.
        PipelineCommon::ShadingSchedulerPass scheduler_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> standardBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> backgroundBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> emissiveBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> gtaoPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipeline_;
    };
}
