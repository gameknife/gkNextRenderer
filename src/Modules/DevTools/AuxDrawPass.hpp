#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/DevTools/AuxDrawSystem.hpp"
#include "Engine/Rendering/ExternalPassRegistry.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Vulkan
{
    class Buffer;
    class DeviceMemory;
    class VulkanBaseRenderer;
}

namespace Vulkan::PipelineCommon
{
    class ZeroBindCustomPushConstantPipeline;
}

namespace Vulkan::AuxDraw
{
    class AuxDrawPass final : public IExternalRenderPass
    {
    public:
        explicit AuxDrawPass(VulkanBaseRenderer& renderer);
        ~AuxDrawPass() override;

        FExternalPassContract Contract() const override;

        void CreateResources() override;
        void ReleaseResources();
        void ReloadShaders(const std::set<std::string>& changedShaderFiles,
                           std::set<std::string>& handledShaderFiles) override;
        void Execute(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

    private:
        struct FrameBuffer
        {
            std::unique_ptr<Buffer> buffer;
            std::unique_ptr<DeviceMemory> memory;
            VkDeviceSize size = 0;
        };

        void EnsureFrameBufferCapacity(uint32_t imageIndex, VkDeviceSize requiredSize);
        void CreateRenderPassAndFramebuffer();
        void RecreateGraphicsPipeline();
        void DestroyGraphicsResources();

        VulkanBaseRenderer& renderer_;
        std::vector<FrameBuffer> frameBuffers_;
        std::vector<DevTools::FAuxPrimitiveGpu> stagingPrimitives_;
        std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
        VkRenderPass renderPass_ = VK_NULL_HANDLE;
        VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };
}
