#pragma once

#include "DebugUtilities.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <string>
#include <vector>

struct VmaAllocator_T;
struct VmaAllocation_T;

namespace Vulkan
{
    struct MemoryHeapStats final
    {
        uint32_t heapIndex{};
        bool deviceLocal{};
        VkDeviceSize heapSizeBytes{};
        VkDeviceSize budgetBytes{};
        VkDeviceSize usageBytes{};
        VkDeviceSize blockBytes{};
        VkDeviceSize allocationBytes{};
        uint32_t blockCount{};
        uint32_t allocationCount{};
    };

    struct MemoryAllocationStats final
    {
        std::string name;
        std::string type;
        VkDeviceSize offsetBytes{};
        VkDeviceSize sizeBytes{};
        VkDeviceSize usageFlags{};
        bool free{};
    };

    struct MemoryBlockStats final
    {
        uint32_t heapIndex{};
        uint32_t memoryTypeIndex{};
        uint32_t blockId{};
        VkDeviceSize blockBytes{};
        VkDeviceSize unusedBytes{};
        uint32_t allocationCount{};
        uint32_t unusedRangeCount{};
        bool dedicated{};
        std::vector<MemoryAllocationStats> allocations;
    };

    struct MemoryStatsSnapshot final
    {
        std::vector<MemoryHeapStats> heaps;
        std::vector<MemoryBlockStats> blocks;

        VkDeviceSize totalHeapSizeBytes{};
        VkDeviceSize totalBudgetBytes{};
        VkDeviceSize totalUsageBytes{};
        VkDeviceSize totalBlockBytes{};
        VkDeviceSize totalAllocationBytes{};

        VkDeviceSize deviceLocalHeapSizeBytes{};
        VkDeviceSize deviceLocalBudgetBytes{};
        VkDeviceSize deviceLocalUsageBytes{};
        VkDeviceSize deviceLocalBlockBytes{};
        VkDeviceSize deviceLocalAllocationBytes{};
    };

    struct MemoryAllocationRequest final
    {
        VkMemoryAllocateFlags allocateFlags{};
        VkMemoryPropertyFlags propertyFlags{};
        bool dedicated = false;
        bool preferRandomAccess = false;
    };

    using VmaAllocatorHandle = ::VmaAllocator_T*;
    using VmaAllocationHandle = ::VmaAllocation_T*;

    struct MemoryAllocationHandle final
    {
        VmaAllocationHandle allocation{};
        VkDeviceMemory deviceMemory{};
    };

    class MemoryAllocator final
    {
    public:

        VULKAN_NON_COPIABLE(MemoryAllocator)

        explicit MemoryAllocator(const Device& device);
        ~MemoryAllocator();

        MemoryAllocationHandle AllocateForBuffer(VkBuffer buffer, const MemoryAllocationRequest& request) const;
        MemoryAllocationHandle AllocateForImage(VkImage image, const MemoryAllocationRequest& request) const;

        void Free(VmaAllocationHandle allocation) const;
        void* Map(VmaAllocationHandle allocation) const;
        void Unmap(VmaAllocationHandle allocation) const;
        void SetAllocationName(VmaAllocationHandle allocation, const char* name) const;

        MemoryStatsSnapshot CaptureStats(bool includeDetails = false) const;

    private:

        const Device& device_;
        VmaAllocatorHandle allocator_{};
    };
}
