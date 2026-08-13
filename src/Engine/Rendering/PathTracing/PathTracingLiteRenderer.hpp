#pragma once

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/PipelineCommon/SamplePostChain.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

namespace Vulkan::PathTracing
{
    // The low-overhead hardware ray-query path deliberately has no SHARC or ReSTIR resources.
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
        std::unique_ptr<PipelineCommon::ZeroBindWithTLASPipeline> rayTracingPipeline_;
        PipelineCommon::SamplePostChain samplePostChain_;
    };
}
