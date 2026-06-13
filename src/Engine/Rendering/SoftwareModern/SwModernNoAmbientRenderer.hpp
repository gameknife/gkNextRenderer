#pragma once

#include "Engine/Rendering/VulkanBaseRenderer.hpp"

#include <memory>

namespace Vulkan
{
    namespace PipelineCommon
    {
        class ZeroBindCustomPushConstantPipeline;
        class ZeroBindPipeline;
    }
}

namespace Vulkan::NoAmbientDeferred
{
    // 不走 AmbientCube 的轻量 deferred renderer：
    //   visibility -> shading -> temporal accumulation -> compose
    class Renderer final : public Vulkan::LogicRendererBase
    {
    public:
        VULKAN_NON_COPIABLE(Renderer)

        explicit Renderer(Vulkan::VulkanBaseRenderer& baseRender);
        ~Renderer();

        void CreateSwapChain(const VkExtent2D& extent) override;
        void DeleteSwapChain() override;
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
        FRendererRequirements Requirements() const override { return GetRendererRequirements(ERT_LegacyDeferredNoAmbient); }

    private:
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> shadingPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> accumulatePipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipeline_;

        uint32_t prevSingleDiffuseId_{};
        int lastRenderedFrame_{-1};
        bool historyValid_{false};
    };
}
