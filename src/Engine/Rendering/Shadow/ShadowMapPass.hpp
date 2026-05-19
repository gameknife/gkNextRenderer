#pragma once

#include "Engine/Vulkan/RenderingPipeline.hpp"

#include <array>
#include <memory>

namespace Assets
{
    class Scene;
}

namespace Vulkan
{
    class CommandPool;
    class Device;
    class SwapChain;
}

namespace Vulkan::Shadow
{
    class ShadowMapPass final
    {
    public:
        VULKAN_NON_COPIABLE(ShadowMapPass)

        ShadowMapPass(const Vulkan::Device& device);
        ~ShadowMapPass();

        // 创建 render pass / pipeline / 4 个 framebuffer。需在 scene 的 sunShadowMap_ 资源就绪后调用。
        void CreateResources(const Assets::Scene& scene);
        void DestroyResources();

        // 绘制 4 个 cascade 的 shadow map。调用前 sun shadow image 应处于 SHADER_READ 状态。
        void Draw(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex);

    private:
        const Vulkan::Device& device_;

        VkRenderPass renderPass_ = VK_NULL_HANDLE;
        std::unique_ptr<class PipelineLayout> pipelineLayout_;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        std::array<VkFramebuffer, 4> frameBuffers_{};
    };
}
