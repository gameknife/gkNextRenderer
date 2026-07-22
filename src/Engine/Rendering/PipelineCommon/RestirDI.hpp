#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

namespace Vulkan
{
    class VulkanBaseRenderer;
}

namespace Vulkan::PipelineCommon
{
    class RestirDI final
    {
    public:
        VULKAN_NON_COPIABLE(RestirDI)

        explicit RestirDI(VulkanBaseRenderer& baseRenderer);
        ~RestirDI();

        void Prepare(VkCommandBuffer commandBuffer, const VkExtent2D& extent, bool reuseAllowed);
        void InsertBarrier(VkCommandBuffer commandBuffer, VkAccessFlags srcAccessMask,
                           VkAccessFlags dstAccessMask) const;

        bool HasResources() const;
        uint32_t FrameStamp() const { return frameStamp_; }
        VkDeviceAddress ReservoirPingAddress() const;
        VkDeviceAddress ReservoirPongAddress() const;
        VkDeviceAddress ParametersAddress() const;
        VkDeviceAddress ResourceTableAddress() const;

    private:
        struct FBufferResource
        {
            // Declaration order makes Buffer die before its bound DeviceMemory.
            std::unique_ptr<Vulkan::DeviceMemory> memory;
            std::unique_ptr<Vulkan::Buffer> buffer;
            VkDeviceSize size = 0;

            void Reset()
            {
                buffer.reset();
                memory.reset();
                size = 0;
            }
        };

        VulkanBaseRenderer& baseRenderer_;
        FBufferResource reservoirPing_;
        FBufferResource reservoirPong_;
        FBufferResource parameters_;
        FBufferResource resourceTable_;
        VkExtent2D extent_{};
        uint32_t lastFrameIndex_ = ~0u;
        uint64_t lastLightsGeneration_ = 0;
        uint64_t lastHistoryGeneration_ = 0;
        uint32_t frameStamp_ = 0;
        bool pendingClear_ = false;

        void EnsureResources(const VkExtent2D& extent);
        void UpdateParameters(bool reuseAllowed);
        void UpdateResourceTable();
        void ClearResources(VkCommandBuffer commandBuffer);
    };
}
