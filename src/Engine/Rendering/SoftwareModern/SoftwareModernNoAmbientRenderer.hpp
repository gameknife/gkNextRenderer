#pragma once

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
        void RenderLegacy(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void RenderSurface(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void ShadeSurface(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene, bool scheduled);
        void ShadeAnalytic(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene);
        void ShadeScheduled(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene);
        void Compose(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene);
        void DispatchGTAO(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        // Legacy monolithic shading (visibility decode + lighting in one dispatch).
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> shadingPipeline_;
        // Primary Surface path: build once, then a lighting-only kernel.
        PipelineCommon::SurfaceBuildPass surfaceBuild_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> surfaceShadingPipeline_;
        // Scheduler path: tile classification + one indirect dispatch per shading bucket.
        PipelineCommon::ShadingSchedulerPass scheduler_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> standardBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> backgroundBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> emissiveBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> gtaoPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipeline_;
    };
}
