#pragma once

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/SamplePostChain.hpp"
#include "Engine/Rendering/PipelineCommon/ShadingSchedulerPass.hpp"
#include "Engine/Rendering/PipelineCommon/SurfaceBuildPass.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <memory>

namespace Vulkan::PipelineCommon
{
}

namespace Vulkan::SoftwareTracing
{
    class SoftwareTracingRenderer final : public Vulkan::LogicRendererBase
    {
    public:
        VULKAN_NON_COPIABLE(SoftwareTracingRenderer)
        
        SoftwareTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender);
        ~SoftwareTracingRenderer();
        
        void CreateSwapChain(const VkExtent2D& extent) override;
        void DeleteSwapChain() override;
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

    private:
        PipelineCommon::SurfaceBuildPass surfaceBuild_;
        PipelineCommon::ShadingSchedulerPass scheduler_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> standardBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> backgroundBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> emissiveBucketPipeline_;
        PipelineCommon::SamplePostChain samplePostChain_;
    };

}
