#include "Allocator.hpp"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "Device.hpp"
#include "Instance.hpp"
#include "Utilities/Exception.hpp"
#include "WindowSurface.hpp"

#include <array>

namespace Vulkan
{
    namespace
    {
        VmaAllocationCreateInfo CreateAllocationInfo(const MemoryAllocationRequest& request)
        {
            VmaAllocationCreateInfo createInfo{};

            const VkMemoryPropertyFlags hostMask =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            const bool wantsHostAccess = (request.propertyFlags & hostMask) != 0;
            const bool wantsDeviceLocal = (request.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;

            createInfo.usage = VMA_MEMORY_USAGE_UNKNOWN;

            if (wantsHostAccess)
            {
                createInfo.requiredFlags |= VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
                createInfo.requiredFlags |= request.propertyFlags &
                    (VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
            }
            else
            {
                createInfo.requiredFlags |= request.propertyFlags;
            }

            if (wantsDeviceLocal && wantsHostAccess)
            {
                createInfo.preferredFlags |= VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            }

            if (request.dedicated)
            {
                createInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            }

            return createInfo;
        }

        MemoryStatsSnapshot BuildStatsSnapshot(const VkPhysicalDeviceMemoryProperties& memoryProperties, const std::array<VmaBudget, VK_MAX_MEMORY_HEAPS>& budgets)
        {
            MemoryStatsSnapshot snapshot{};
            snapshot.heaps.reserve(memoryProperties.memoryHeapCount);

            for (uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex)
            {
                const VkMemoryHeap& heap = memoryProperties.memoryHeaps[heapIndex];
                const VmaBudget& budget = budgets[heapIndex];

                MemoryHeapStats heapStats{};
                heapStats.heapIndex = heapIndex;
                heapStats.deviceLocal = (heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
                heapStats.heapSizeBytes = heap.size;
                heapStats.budgetBytes = budget.budget;
                heapStats.usageBytes = budget.usage;
                heapStats.blockBytes = budget.statistics.blockBytes;
                heapStats.allocationBytes = budget.statistics.allocationBytes;
                heapStats.blockCount = budget.statistics.blockCount;
                heapStats.allocationCount = budget.statistics.allocationCount;
                snapshot.heaps.push_back(heapStats);

                snapshot.totalHeapSizeBytes += heapStats.heapSizeBytes;
                snapshot.totalBudgetBytes += heapStats.budgetBytes;
                snapshot.totalUsageBytes += heapStats.usageBytes;
                snapshot.totalBlockBytes += heapStats.blockBytes;
                snapshot.totalAllocationBytes += heapStats.allocationBytes;

                if (heapStats.deviceLocal)
                {
                    snapshot.deviceLocalHeapSizeBytes += heapStats.heapSizeBytes;
                    snapshot.deviceLocalBudgetBytes += heapStats.budgetBytes;
                    snapshot.deviceLocalUsageBytes += heapStats.usageBytes;
                    snapshot.deviceLocalBlockBytes += heapStats.blockBytes;
                    snapshot.deviceLocalAllocationBytes += heapStats.allocationBytes;
                }
            }

            return snapshot;
        }
    }

    MemoryAllocator::MemoryAllocator(const Device& device)
        : device_(device)
    {
        VkPhysicalDeviceProperties physicalDeviceProperties{};
        vkGetPhysicalDeviceProperties(device_.PhysicalDevice(), &physicalDeviceProperties);

        VmaAllocatorCreateInfo createInfo{};
        createInfo.flags =
            VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT |
            VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
        createInfo.physicalDevice = device_.PhysicalDevice();
        createInfo.device = device_.Handle();
        createInfo.instance = device_.Surface().Instance().Handle();
        createInfo.vulkanApiVersion = VK_API_VERSION_1_2;

        Check(vmaCreateAllocator(&createInfo, &allocator_), "create VMA allocator");
    }

    MemoryAllocator::~MemoryAllocator()
    {
        if (allocator_ != nullptr)
        {
            vmaDestroyAllocator(allocator_);
            allocator_ = nullptr;
        }
    }

    MemoryAllocationHandle MemoryAllocator::AllocateForBuffer(VkBuffer buffer, const MemoryAllocationRequest& request) const
    {
        const VmaAllocationCreateInfo createInfo = CreateAllocationInfo(request);

        VmaAllocation allocation = nullptr;
        VmaAllocationInfo allocationInfo{};
        Check(vmaAllocateMemoryForBuffer(allocator_, buffer, &createInfo, &allocation, &allocationInfo),
            "allocate VMA buffer memory");
        Check(vmaBindBufferMemory(allocator_, allocation, buffer),
            "bind VMA buffer memory");

        return {
            allocation,
            allocationInfo.deviceMemory
        };
    }

    MemoryAllocationHandle MemoryAllocator::AllocateForImage(VkImage image, const MemoryAllocationRequest& request) const
    {
        const VmaAllocationCreateInfo createInfo = CreateAllocationInfo(request);

        VmaAllocation allocation = nullptr;
        VmaAllocationInfo allocationInfo{};
        Check(vmaAllocateMemoryForImage(allocator_, image, &createInfo, &allocation, &allocationInfo),
            "allocate VMA image memory");
        Check(vmaBindImageMemory(allocator_, allocation, image),
            "bind VMA image memory");

        return {
            allocation,
            allocationInfo.deviceMemory
        };
    }

    void MemoryAllocator::Free(VmaAllocationHandle allocation) const
    {
        if (allocation != nullptr)
        {
            vmaFreeMemory(allocator_, allocation);
        }
    }

    void* MemoryAllocator::Map(VmaAllocationHandle allocation) const
    {
        void* data = nullptr;
        Check(vmaMapMemory(allocator_, allocation, &data), "map VMA memory");
        return data;
    }

    void MemoryAllocator::Unmap(VmaAllocationHandle allocation) const
    {
        vmaUnmapMemory(allocator_, allocation);
    }

    void MemoryAllocator::SetAllocationName(VmaAllocationHandle allocation, const char* name) const
    {
        if (allocation != nullptr && name != nullptr && name[0] != '\0')
        {
            vmaSetAllocationName(allocator_, allocation, name);
        }
    }

    MemoryStatsSnapshot MemoryAllocator::CaptureStats() const
    {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(device_.PhysicalDevice(), &memoryProperties);

        std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
        vmaGetHeapBudgets(allocator_, budgets.data());

        return BuildStatsSnapshot(memoryProperties, budgets);
    }
}
