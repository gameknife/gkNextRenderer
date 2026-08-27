#include "Engine/Vulkan/Allocator.hpp"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

#include <array>
#include <charconv>
#include <nlohmann/json.hpp>

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

        bool TryParseIndexedKey(const std::string& key, const char* prefix, uint32_t& outIndex)
        {
            const std::string_view keyView(key);
            const std::string_view prefixView(prefix);
            if (!keyView.starts_with(prefixView))
            {
                return false;
            }

            const std::string_view numberView = keyView.substr(prefixView.size());
            const char* begin = numberView.data();
            const char* end = begin + numberView.size();
            uint32_t value = 0;
            const auto result = std::from_chars(begin, end, value);
            if (result.ec != std::errc{} || result.ptr != end)
            {
                return false;
            }

            outIndex = value;
            return true;
        }

        VkDeviceSize JsonDeviceSize(const nlohmann::json& object, const char* key)
        {
            if (!object.contains(key) || !object[key].is_number_unsigned())
            {
                return 0;
            }
            return static_cast<VkDeviceSize>(object[key].get<uint64_t>());
        }

        uint32_t JsonUint32(const nlohmann::json& object, const char* key)
        {
            if (!object.contains(key) || !object[key].is_number_unsigned())
            {
                return 0;
            }
            return object[key].get<uint32_t>();
        }

        std::string JsonString(const nlohmann::json& object, const char* key)
        {
            if (!object.contains(key) || !object[key].is_string())
            {
                return {};
            }
            return object[key].get<std::string>();
        }

        void SetDefaultAllocationName(VmaAllocatorHandle allocator, VmaAllocationHandle allocation, const char* name)
        {
            if (allocator != nullptr && allocation != nullptr && name != nullptr && name[0] != '\0')
            {
                vmaSetAllocationName(allocator, allocation, name);
            }
        }

        uint32_t MemoryTypeToHeapIndex(const VkPhysicalDeviceMemoryProperties& memoryProperties, uint32_t memoryTypeIndex)
        {
            return memoryTypeIndex < memoryProperties.memoryTypeCount
                ? memoryProperties.memoryTypes[memoryTypeIndex].heapIndex
                : 0;
        }

        MemoryAllocationStats ParseAllocationStats(const nlohmann::json& allocationJson)
        {
            MemoryAllocationStats allocation{};
            allocation.name = JsonString(allocationJson, "Name");
            allocation.type = JsonString(allocationJson, "Type");
            allocation.offsetBytes = JsonDeviceSize(allocationJson, "Offset");
            allocation.sizeBytes = JsonDeviceSize(allocationJson, "Size");
            allocation.usageFlags = JsonDeviceSize(allocationJson, "Usage");
            allocation.free = allocation.type == "FREE";
            if (allocation.name.empty() && allocation.free)
            {
                allocation.name = "(free)";
            }
            return allocation;
        }

        void AppendBlockDetailsFromType(MemoryStatsSnapshot& snapshot,
                                        const nlohmann::json& typeObject,
                                        const VkPhysicalDeviceMemoryProperties& memoryProperties,
                                        uint32_t memoryTypeIndex)
        {
            if (!typeObject.contains("Blocks") || !typeObject["Blocks"].is_object())
            {
                return;
            }

            for (const auto& [blockKey, blockJson] : typeObject["Blocks"].items())
            {
                uint32_t blockId = 0;
                if (!blockJson.is_object() || !TryParseIndexedKey(blockKey, "", blockId))
                {
                    continue;
                }

                MemoryBlockStats block{};
                block.heapIndex = MemoryTypeToHeapIndex(memoryProperties, memoryTypeIndex);
                block.memoryTypeIndex = memoryTypeIndex;
                block.blockId = blockId;
                block.blockBytes = JsonDeviceSize(blockJson, "TotalBytes");
                block.unusedBytes = JsonDeviceSize(blockJson, "UnusedBytes");
                block.allocationCount = JsonUint32(blockJson, "Allocations");
                block.unusedRangeCount = JsonUint32(blockJson, "UnusedRanges");

                if (blockJson.contains("Suballocations") && blockJson["Suballocations"].is_array())
                {
                    block.allocations.reserve(blockJson["Suballocations"].size());
                    for (const nlohmann::json& allocationJson : blockJson["Suballocations"])
                    {
                        if (allocationJson.is_object())
                        {
                            block.allocations.push_back(ParseAllocationStats(allocationJson));
                        }
                    }
                }

                snapshot.blocks.push_back(std::move(block));
            }
        }

        void AppendDedicatedAllocationDetailsFromType(MemoryStatsSnapshot& snapshot,
                                                      const nlohmann::json& typeObject,
                                                      const VkPhysicalDeviceMemoryProperties& memoryProperties,
                                                      uint32_t memoryTypeIndex)
        {
            if (!typeObject.contains("DedicatedAllocations") || !typeObject["DedicatedAllocations"].is_array())
            {
                return;
            }

            uint32_t dedicatedIndex = 0;
            for (const nlohmann::json& allocationJson : typeObject["DedicatedAllocations"])
            {
                if (!allocationJson.is_object())
                {
                    continue;
                }

                MemoryAllocationStats allocation = ParseAllocationStats(allocationJson);
                MemoryBlockStats block{};
                block.heapIndex = MemoryTypeToHeapIndex(memoryProperties, memoryTypeIndex);
                block.memoryTypeIndex = memoryTypeIndex;
                block.blockId = dedicatedIndex++;
                block.blockBytes = allocation.sizeBytes;
                block.unusedBytes = 0;
                block.allocationCount = 1;
                block.dedicated = true;
                block.allocations.push_back(std::move(allocation));
                snapshot.blocks.push_back(std::move(block));
            }
        }

        void AppendDefaultPoolDetails(MemoryStatsSnapshot& snapshot,
                                      const nlohmann::json& statsJson,
                                      const VkPhysicalDeviceMemoryProperties& memoryProperties)
        {
            if (!statsJson.contains("DefaultPools") || !statsJson["DefaultPools"].is_object())
            {
                return;
            }

            for (const auto& [typeKey, typeObject] : statsJson["DefaultPools"].items())
            {
                uint32_t memoryTypeIndex = 0;
                if (!typeObject.is_object() || !TryParseIndexedKey(typeKey, "Type ", memoryTypeIndex))
                {
                    continue;
                }
                AppendBlockDetailsFromType(snapshot, typeObject, memoryProperties, memoryTypeIndex);
                AppendDedicatedAllocationDetailsFromType(snapshot, typeObject, memoryProperties, memoryTypeIndex);
            }
        }

        void AppendCustomPoolDetails(MemoryStatsSnapshot& snapshot,
                                     const nlohmann::json& statsJson,
                                     const VkPhysicalDeviceMemoryProperties& memoryProperties)
        {
            if (!statsJson.contains("CustomPools") || !statsJson["CustomPools"].is_object())
            {
                return;
            }

            for (const auto& [typeKey, poolsJson] : statsJson["CustomPools"].items())
            {
                uint32_t memoryTypeIndex = 0;
                if (!poolsJson.is_array() || !TryParseIndexedKey(typeKey, "Type ", memoryTypeIndex))
                {
                    continue;
                }

                for (const nlohmann::json& poolJson : poolsJson)
                {
                    if (poolJson.is_object())
                    {
                        AppendBlockDetailsFromType(snapshot, poolJson, memoryProperties, memoryTypeIndex);
                        AppendDedicatedAllocationDetailsFromType(snapshot, poolJson, memoryProperties, memoryTypeIndex);
                    }
                }
            }
        }

        void AppendStatsStringDetails(MemoryStatsSnapshot& snapshot,
                                      const VkPhysicalDeviceMemoryProperties& memoryProperties,
                                      const char* statsString)
        {
            if (statsString == nullptr || statsString[0] == '\0')
            {
                return;
            }

            const nlohmann::json statsJson = nlohmann::json::parse(statsString, nullptr, false);
            if (statsJson.is_discarded() || !statsJson.is_object())
            {
                return;
            }

            AppendDefaultPoolDetails(snapshot, statsJson, memoryProperties);
            AppendCustomPoolDetails(snapshot, statsJson, memoryProperties);
        }
    }

    MemoryAllocator::MemoryAllocator(const Device& device)
        : device_(device)
    {
        VkPhysicalDeviceProperties physicalDeviceProperties{};
        vkGetPhysicalDeviceProperties(device_.PhysicalDevice(), &physicalDeviceProperties);

        VmaAllocatorCreateInfo createInfo{};
        // The device-address flag makes VMA add VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT to every
        // allocation, which is invalid when the feature was not enabled at device creation.
        createInfo.flags =
            VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT |
            (device_.SupportsBufferDeviceAddress() ? VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT : 0);
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
        SetDefaultAllocationName(allocator_, allocation, "VMA Buffer Memory");

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
        SetDefaultAllocationName(allocator_, allocation, "VMA Image Memory");

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

    MemoryStatsSnapshot MemoryAllocator::CaptureStats(bool includeDetails) const
    {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(device_.PhysicalDevice(), &memoryProperties);

        std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
        vmaGetHeapBudgets(allocator_, budgets.data());

        MemoryStatsSnapshot snapshot = BuildStatsSnapshot(memoryProperties, budgets);
        if (includeDetails)
        {
            char* statsString = nullptr;
            vmaBuildStatsString(allocator_, &statsString, VK_TRUE);
            AppendStatsStringDetails(snapshot, memoryProperties, statsString);
            vmaFreeStatsString(allocator_, statsString);
        }

        return snapshot;
    }
}
