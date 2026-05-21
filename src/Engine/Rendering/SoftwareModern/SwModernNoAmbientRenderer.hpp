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

namespace Vulkan::NoAmbientDeferred
{
    // 不走 AmbientCube 的轻量 deferred renderer：
    //   visibility (PreRender 共享) -> Core.SwModernNoAmbient.comp -> Process.ComposeSimple.comp
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
        std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipeline_;
    };
}
