#pragma once

#include "Engine/Assets/Core/GaussianSplat.hpp"
#include "Engine/Rendering/ExternalPassRegistry.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <glm/mat4x4.hpp>
#include <set>
#include <string>

namespace Vulkan::PipelineCommon
{
    class ZeroBindCustomPushConstantPipeline;
}

namespace Vulkan::GaussianSplat
{
    class GaussianSplatPass final : public IExternalRenderPass
    {
    public:
        VULKAN_NON_COPIABLE(GaussianSplatPass)

        explicit GaussianSplatPass(VulkanBaseRenderer& renderer);
        ~GaussianSplatPass() override;

        void CreateResources() override;
        void DestroyResources();
        void ReloadShaders(const std::set<std::string>& changedShaderFiles,
                           std::set<std::string>& handledShaderFiles) override;
        void Execute(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

    private:
        void UpdateModelStates(uint32_t imageIndex);
        void DispatchGpuSort(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void RecreateGraphicsPipeline();

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
        std::unique_ptr<Buffer> splatBucketBuffer_;
        std::unique_ptr<DeviceMemory> splatBucketMemory_;
        std::unique_ptr<Buffer> groupBucketCountBuffer_;
        std::unique_ptr<DeviceMemory> groupBucketCountMemory_;
        std::unique_ptr<Buffer> groupBucketOffsetBuffer_;
        std::unique_ptr<DeviceMemory> groupBucketOffsetMemory_;
        std::unique_ptr<Buffer> drawIndirectBuffer_;
        std::unique_ptr<DeviceMemory> drawIndirectMemory_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> histogramPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> prefixPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> groupScanPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> scatterPipeline_;
        std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> composePipeline_;
        uint32_t splatCount_ = 0;
        uint32_t modelCount_ = 0;
        uint32_t sortBucketCapacity_ = 0;
        uint32_t sortGroupCountCapacity_ = 0;
        uint64_t currentSortModelStateHash_ = 0;
        uint64_t lastSortCacheKey_ = 0;
        bool sortCacheValid_ = false;
    };
}
