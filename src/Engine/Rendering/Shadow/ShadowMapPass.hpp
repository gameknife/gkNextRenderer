#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <array>
#include <memory>
#include <set>
#include <string>

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
        void ReloadShaders(const std::set<std::string>& changedShaderFiles, std::set<std::string>& handledShaderFiles);

        // 绘制单个 cascade 的 shadow map。调用前 soft mesh shader draw args 已完成 GPU cull。
        void DrawCascade(VkCommandBuffer commandBuffer, const Assets::Scene& scene, const Assets::GPUScene& gpuScene,
                         uint32_t cascade);

    private:
        const Vulkan::Device& device_;

        VkRenderPass renderPass_ = VK_NULL_HANDLE;
        std::unique_ptr<class PipelineLayout> pipelineLayout_;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        std::array<VkFramebuffer, 4> frameBuffers_{};

        void RecreatePipeline();
    };
}
