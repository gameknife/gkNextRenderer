#pragma once

#include "Engine/Assets/Core/GaussianSplat.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <glm/mat4x4.hpp>

namespace Vulkan::PipelineCommon
{
    class ZeroBindCustomPushConstantPipeline;
}

namespace Vulkan::GaussianSplat
{
    class GaussianSplatPass final
    {
    public:
        VULKAN_NON_COPIABLE(GaussianSplatPass)

        explicit GaussianSplatPass(VulkanBaseRenderer& renderer);
        ~GaussianSplatPass();

        void CreateResources();
        void DestroyResources();
        void Execute(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    private:
        void UpdateModelStates(uint32_t imageIndex);
        void DispatchGpuSort(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        VulkanBaseRenderer& renderer_;
        VkRenderPass renderPass_ = VK_NULL_HANDLE;
        VkFramebuffer frameBuffer_ = VK_NULL_HANDLE;
        std::unique_ptr<PipelineLayout> pipelineLayout_;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
        std::unique_ptr<Buffer> splatBuffer_;
        std::unique_ptr<DeviceMemory> splatMemory_;
        std::unique_ptr<Buffer> paletteBuffer_;
        std::unique_ptr<DeviceMemory> paletteMemory_;
        std::vector<std::unique_ptr<Buffer>> modelStateBuffers_;
        std::vector<std::unique_ptr<DeviceMemory>> modelStateMemories_;
        std::vector<void*> mappedModelStates_;
        std::unique_ptr<Buffer> sortedIndexBuffer_;
        std::unique_ptr<DeviceMemory> sortedIndexMemory_;
        std::unique_ptr<Buffer> bucketCountBuffer_;
        std::unique_ptr<DeviceMemory> bucketCountMemory_;
        std::unique_ptr<Buffer> bucketOffsetBuffer_;
        std::unique_ptr<DeviceMemory> bucketOffsetMemory_;
        std::unique_ptr<Buffer> bucketCursorBuffer_;
        std::unique_ptr<DeviceMemory> bucketCursorMemory_;
        std::unique_ptr<Buffer> drawIndirectBuffer_;
        std::unique_ptr<DeviceMemory> drawIndirectMemory_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> histogramPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> prefixPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> scatterPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> composePipeline_;
        uint32_t splatCount_ = 0;
        uint32_t modelCount_ = 0;
    };
}
