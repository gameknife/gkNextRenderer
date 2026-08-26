#pragma once

#include "Engine/Vulkan/Allocator.hpp"
#include <atomic>
#include <map>
#include <memory>
#include <vector>

#include "Engine/Vulkan/DebugUtilities.hpp"

namespace Vulkan
{
    class Surface;
    class DeviceProcedures;

    class Device final
    {
    public:

        VULKAN_NON_COPIABLE(Device)

        Device(
            VkPhysicalDevice physicalDevice, 
            const Surface& surface, 
            const std::vector<const char*>& requiredExtensionsconst,
            const VkPhysicalDeviceFeatures& deviceFeatures,
            const void* nextDeviceFeatures);
        
        ~Device();

        VkPhysicalDevice PhysicalDevice() const { return physicalDevice_; }
        const class Surface& Surface() const { return surface_; }

        const class DebugUtils& DebugUtils() const { return debugUtils_; }

        uint32_t GraphicsFamilyIndex() const { return graphicsFamilyIndex_; }
        uint32_t ComputeFamilyIndex() const { return computeFamilyIndex_; }
        uint32_t PresentFamilyIndex() const { return presentFamilyIndex_; }
        int32_t TransferFamilyIndex() const { return transferFamilyIndex_; }

        VkQueue GraphicsQueue() const { return graphicsQueue_; }
        VkQueue ComputeQueue() const { return computeQueue_; }
        VkQueue PresentQueue() const { return presentQueue_; }
        VkQueue TransferQueue() const { return transferQueue_; }
        // Also resolves queues created for augmenter-requested extra families.
        VkQueue QueueForFamilyIndex(uint32_t queueFamilyIndex) const;
        VkQueue QueueForFamilyIndex(uint32_t queueFamilyIndex, uint32_t queueIndex) const;
        uint32_t CreatedQueueCount(uint32_t queueFamilyIndex) const;

        VkPhysicalDeviceProperties DeviceProperties() const { return deviceProp_; }
        // Whether vkCreateDevice actually enabled bufferDeviceAddress. Almost the whole engine
        // reaches scene data through addresses in the GPUScene push constant, so a device without
        // it can only run the compatibility renderer, which binds its buffers explicitly.
        // Buffer masks the usage bit and returns a null address when this is false, so a buffer
        // built for the normal path stays creatable -- it just cannot be addressed.
        bool SupportsBufferDeviceAddress() const { return bufferDeviceAddressEnabled_; }
        MemoryStatsSnapshot CaptureMemoryStats(bool includeDetails = false) const;

        void WaitIdle() const;

        // The cache is shared by every graphics and compute pipeline created for this logical
        // device. It is populated from the per-user cache directory during device creation and
        // can be persisted after a warm-up batch as well as during shutdown.
        VkPipelineCache PipelineCache() const { return pipelineCache_; }
        void PersistPipelineCache() const;
        void RecordPipelineCreated(const char* label) const;
        uint32_t PipelineCreationCount() const { return pipelineCreationCount_.load(); }

        const DeviceProcedures& GetDeviceProcedures() const { return *deviceProcedures_; }
        const MemoryAllocator& GetMemoryAllocator() const { return *memoryAllocator_; }

    private:

        void CheckRequiredExtensions(VkPhysicalDevice physicalDevice, const std::vector<const char*>& requiredExtensions) const;

        const VkPhysicalDevice physicalDevice_;
        const class Surface& surface_;

        VULKAN_HANDLE(VkDevice, device_)

        class DebugUtils debugUtils_;

        uint32_t graphicsFamilyIndex_ {};
        uint32_t computeFamilyIndex_{};
        uint32_t presentFamilyIndex_{};
        uint32_t transferFamilyIndex_{};

        VkQueue graphicsQueue_{};
        VkQueue computeQueue_{};
        VkQueue presentQueue_{};
        VkQueue transferQueue_{};
        // Queues for augmenter-requested extra families (e.g. video encode).
        std::map<uint32_t, VkQueue> extraQueues_;
        std::map<uint32_t, std::vector<VkQueue>> createdQueues_;
                
        std::unique_ptr<DeviceProcedures> deviceProcedures_;
        std::unique_ptr<MemoryAllocator> memoryAllocator_;
        VkPhysicalDeviceProperties deviceProp_;
        VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
        mutable std::atomic<uint32_t> pipelineCreationCount_ = 0;
        bool bufferDeviceAddressEnabled_ = false;
    };

}
