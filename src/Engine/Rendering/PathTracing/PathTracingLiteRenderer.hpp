#pragma once

// Core renderer implementation owned by gkNextEngine.

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/PipelineCommon/SamplePostChain.hpp"
#include "Engine/Rendering/PipelineCommon/ShadingSchedulerPass.hpp"
#include "Engine/Rendering/PipelineCommon/SurfaceBuildPass.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

namespace Vulkan::PathTracing
{
    // The low-overhead hardware ray-query path deliberately has no SHARC or ReSTIR resources -- and
    // must keep it that way: it shades from the Primary Surface, and the tile scheduler owns
    // GPUScene.ReservedAddress0 / CustomData1, which is where the tracing extras table would live.
    class PathTracingLiteRenderer final : public Vulkan::LogicRendererBase
    {
    public:
        VULKAN_NON_COPIABLE(PathTracingLiteRenderer);

        PathTracingLiteRenderer(Vulkan::VulkanBaseRenderer& baseRender): LogicRendererBase(baseRender) {}
        ~PathTracingLiteRenderer() override;

        void CreateSwapChain(const VkExtent2D& extent) override;
        void DeleteSwapChain() override;
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

    private:
        PipelineCommon::SurfaceBuildPass surfaceBuild_;
        PipelineCommon::ShadingSchedulerPass scheduler_;
        // Standard hardware-traces its secondary rays; the two terminal buckets are the shared
        // tracing-family kernels and need no TLAS.
        std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> standardBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> backgroundBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> emissiveBucketPipeline_;
        PipelineCommon::SamplePostChain samplePostChain_;
    };
}
