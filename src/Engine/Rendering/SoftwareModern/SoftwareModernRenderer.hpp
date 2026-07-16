#pragma once

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/TemporalPostChain.hpp"
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
        PipelineCommon::TemporalPostChain temporalPostChain_;

    };

}
