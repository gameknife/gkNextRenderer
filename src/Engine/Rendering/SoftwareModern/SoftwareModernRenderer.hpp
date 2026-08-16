#pragma once

#include "Engine/Rendering/PipelineCommon/ShadingSchedulerPass.hpp"
#include "Engine/Rendering/PipelineCommon/SurfaceBuildPass.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/SamplePostChain.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <memory>
#include <string>

namespace Vulkan::SoftwareModern
{
    class SoftwareModernRenderer final : public Vulkan::LogicRendererBase
    {
    public:
        VULKAN_NON_COPIABLE(SoftwareModernRenderer)

        explicit SoftwareModernRenderer(Vulkan::VulkanBaseRenderer& baseRender);
        ~SoftwareModernRenderer();

        void CreateSwapChain(const VkExtent2D& extent) override;
        void DeleteSwapChain() override;
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

    private:
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> deferredShadingPipeline_;
        PipelineCommon::SurfaceBuildPass surfaceBuild_;
        PipelineCommon::ShadingSchedulerPass scheduler_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> standardBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> backgroundBucketPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> emissiveBucketPipeline_;

        PipelineCommon::SamplePostChain samplePostChain_;

    };

}
