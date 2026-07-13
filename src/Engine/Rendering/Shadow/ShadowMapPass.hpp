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

        // Create the render pass, pipeline, and four framebuffers after the scene's sunShadowMap_ is ready.
        void CreateResources(const Assets::Scene& scene);
        void DestroyResources();
        void ReloadShaders(const std::set<std::string>& changedShaderFiles, std::set<std::string>& handledShaderFiles);

        // Draw one cascade shadow map after GPU culling has populated the soft-mesh shader draw arguments.
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
