#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <memory>
#include <vector>

namespace Vulkan::PipelineCommon
{
    class ZeroBindWithTLASPipeline : public PipelineBase
    {
    public:
        VULKAN_NON_COPIABLE(ZeroBindWithTLASPipeline)
        ZeroBindWithTLASPipeline(const SwapChain& swapChain, const char* shaderfile, const Assets::Scene& scene,
                                 VkAccelerationStructureKHR accelerationStructureHandle);

        void BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex);
        void BindPipeline(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene);
    };
    
    class ZeroBindPipeline : public PipelineBase
    {
    public:
        VULKAN_NON_COPIABLE(ZeroBindPipeline)
        ZeroBindPipeline(const SwapChain& swapChain, const char* shaderfile, const Assets::Scene& scene);

        void BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex);
        void BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex,
                          uint32_t customData0, uint32_t customData1, uint32_t customData2);
        void BindPipeline(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene);
        void BindPipeline(VkCommandBuffer commandBuffer, const Assets::GPUScene& gpuScene,
                          uint32_t customData0, uint32_t customData1, uint32_t customData2);
    };
    
    class ZeroBindCustomPushConstantPipeline : public PipelineBase
    {
    public:
        VULKAN_NON_COPIABLE(ZeroBindCustomPushConstantPipeline)
        ZeroBindCustomPushConstantPipeline(const SwapChain& swapChain, const char* shaderfile, uint32_t pushConstantSize);
        void BindPipeline(VkCommandBuffer commandBuffer, const void* data)
        {
            BindPipeline(commandBuffer, data, 0);
        }
        void BindPipeline(VkCommandBuffer commandBuffer, const void* data, uint32_t viewBankBase);

    private:
        uint32_t pushConstantSize_;
    };

    class VisibilityPipeline : public PipelineBase
    {
    public:
        VULKAN_NON_COPIABLE(VisibilityPipeline)

        VisibilityPipeline(
            const SwapChain& swapChain, 
            const DepthBuffer& depthBuffer,
            const std::vector<Assets::UniformBuffer>& uniformBuffers,
            const Assets::Scene& scene);
        ~VisibilityPipeline();
        
        const Vulkan::RenderPass& RenderPass() const { return *renderPass_; }

    private:
        std::unique_ptr<Vulkan::RenderPass> renderPass_;
    };

    class GraphicsPipeline : public PipelineBase
    {
    public:

        VULKAN_NON_COPIABLE(GraphicsPipeline)

        GraphicsPipeline(
            const SwapChain& swapChain, 
            const DepthBuffer& depthBuffer,
            const std::vector<Assets::UniformBuffer>& uniformBuffers,
            const Assets::Scene& scene,
            bool isWireFrame);
        ~GraphicsPipeline();
        
        const class RenderPass& RenderPass() const { return *renderPass_; }

    private:
        std::unique_ptr<class RenderPass> renderPass_;
    };
}
