#pragma once

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
        void ReloadShaders(const std::set<std::string>& changedShaderFiles, std::set<std::string>& handledShaderFiles) override;
        FRendererRequirements Requirements() const override { return GetRendererRequirements(ERT_SoftwareModernNoAmbient); }
        bool RequiresObjectIdHistory() const override { return false; }

    private:
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> shadingPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> gtaoPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipeline_;

    };
}
