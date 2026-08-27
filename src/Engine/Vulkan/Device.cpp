#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"
#include "Engine/Vulkan/DeviceCreationAugmenter.hpp"
#include <algorithm>
#include <cstring>
#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <fmt/format.h>

namespace Vulkan {

namespace
{
    constexpr uint32_t PipelineCacheFileVersion = 1;
    constexpr uint64_t MaxPipelineCacheBytes = 128ull * 1024ull * 1024ull;

    struct PipelineCacheFileHeader
    {
        std::array<char, 8> magic{'G', 'K', 'P', 'S', 'O', 'C', 'A', 'C'};
        uint32_t version = PipelineCacheFileVersion;
        uint32_t vendorId = 0;
        uint32_t deviceId = 0;
        uint32_t driverVersion = 0;
        std::array<uint8_t, VK_UUID_SIZE> pipelineCacheUuid{};
        uint64_t dataSize = 0;
    };

    std::filesystem::path GetPipelineCachePath(const VkPhysicalDeviceProperties& properties)
    {
        std::string uuid;
        uuid.reserve(VK_UUID_SIZE * 2);
        for (const uint8_t byte : properties.pipelineCacheUUID)
        {
            uuid += fmt::format("{:02x}", byte);
        }
        const std::string fileName = fmt::format("cache/vulkan_pipeline_{:04x}_{:04x}_{:08x}_{}.bin",
                                                  properties.vendorID, properties.deviceID,
                                                  properties.driverVersion, uuid);
        return Utilities::FileHelper::GetWritableFilePath(fileName.c_str());
    }

    std::vector<uint8_t> LoadPipelineCacheData(const VkPhysicalDeviceProperties& properties)
    {
        const std::filesystem::path path = GetPipelineCachePath(properties);
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            SPDLOG_INFO("Vulkan pipeline cache: no disk cache at {}", path.string());
            return {};
        }

        const std::streamsize fileSize = input.tellg();
        if (fileSize < static_cast<std::streamsize>(sizeof(PipelineCacheFileHeader)))
        {
            SPDLOG_WARN("Vulkan pipeline cache: ignoring truncated file {}", path.string());
            return {};
        }
        input.seekg(0);

        PipelineCacheFileHeader header{};
        input.read(reinterpret_cast<char*>(&header), sizeof(header));
        const bool compatible = header.magic == PipelineCacheFileHeader{}.magic &&
                                header.version == PipelineCacheFileVersion &&
                                header.vendorId == properties.vendorID &&
                                header.deviceId == properties.deviceID &&
                                header.driverVersion == properties.driverVersion &&
                                header.pipelineCacheUuid == std::array<uint8_t, VK_UUID_SIZE>{
                                    properties.pipelineCacheUUID[0], properties.pipelineCacheUUID[1],
                                    properties.pipelineCacheUUID[2], properties.pipelineCacheUUID[3],
                                    properties.pipelineCacheUUID[4], properties.pipelineCacheUUID[5],
                                    properties.pipelineCacheUUID[6], properties.pipelineCacheUUID[7],
                                    properties.pipelineCacheUUID[8], properties.pipelineCacheUUID[9],
                                    properties.pipelineCacheUUID[10], properties.pipelineCacheUUID[11],
                                    properties.pipelineCacheUUID[12], properties.pipelineCacheUUID[13],
                                    properties.pipelineCacheUUID[14], properties.pipelineCacheUUID[15]};
        if (!compatible || header.dataSize > MaxPipelineCacheBytes ||
            header.dataSize != static_cast<uint64_t>(fileSize - sizeof(PipelineCacheFileHeader)))
        {
            SPDLOG_WARN("Vulkan pipeline cache: ignoring incompatible file {}", path.string());
            return {};
        }

        std::vector<uint8_t> data(static_cast<size_t>(header.dataSize));
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!input)
        {
            SPDLOG_WARN("Vulkan pipeline cache: failed to read {}", path.string());
            return {};
        }
        SPDLOG_INFO("Vulkan pipeline cache: loaded {} KiB from {}", data.size() / 1024, path.string());
        return data;
    }

    void CreatePipelineCache(VkDevice device, const VkPhysicalDeviceProperties& properties, VkPipelineCache& cache)
    {
        const std::vector<uint8_t> initialData = LoadPipelineCacheData(properties);
        VkPipelineCacheCreateInfo createInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
        createInfo.initialDataSize = initialData.size();
        createInfo.pInitialData = initialData.empty() ? nullptr : initialData.data();

        VkResult result = vkCreatePipelineCache(device, &createInfo, nullptr, &cache);
        if (result != VK_SUCCESS && !initialData.empty())
        {
            SPDLOG_WARN("Vulkan pipeline cache: driver rejected disk data ({}); rebuilding", static_cast<int>(result));
            createInfo.initialDataSize = 0;
            createInfo.pInitialData = nullptr;
            result = vkCreatePipelineCache(device, &createInfo, nullptr, &cache);
        }
        Check(result, "create Vulkan pipeline cache");
    }

    std::vector<VkQueueFamilyProperties>::const_iterator FindQueue(
        const std::vector<VkQueueFamilyProperties>& queueFamilies,
        const std::string& name,
        const VkQueueFlags requiredBits,
        const VkQueueFlags excludedBits,
        uint32_t minCount)
    {
        const auto family = std::find_if(queueFamilies.begin(), queueFamilies.end(), [requiredBits, excludedBits, minCount](const VkQueueFamilyProperties& queueFamily)
        {
            return 
                queueFamily.queueCount >= minCount && 
                queueFamily.queueFlags & requiredBits &&
                !(queueFamily.queueFlags & excludedBits);
        });

        if (family == queueFamilies.end())
        {
            Throw(std::runtime_error(fmt::format("found no matching {} queue", name)));
        }

        return family;
    }

    // The enabled-features chain is handed to us as an opaque pNext list, so read the answer from
    // the same structure vkCreateDevice will act on rather than re-querying the physical device --
    // the caller may have masked the feature off even when the device advertises it.
    bool IsBufferDeviceAddressEnabled(const void* nextDeviceFeatures)
    {
        for (const auto* entry = static_cast<const VkBaseInStructure*>(nextDeviceFeatures);
             entry != nullptr; entry = entry->pNext)
        {
            if (entry->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES)
            {
                return reinterpret_cast<const VkPhysicalDeviceBufferDeviceAddressFeatures*>(entry)
                    ->bufferDeviceAddress != VK_FALSE;
            }
        }
        return false;
    }
}

Device::Device(
    VkPhysicalDevice physicalDevice, 
    const class Surface& surface, 
    const std::vector<const char*>& requiredExtensions,
    const VkPhysicalDeviceFeatures& deviceFeatures,
    const void* nextDeviceFeatures) :
    physicalDevice_(physicalDevice),
    surface_(surface),
    debugUtils_(surface.Instance().Handle()),
    bufferDeviceAddressEnabled_(IsBufferDeviceAddressEnabled(nextDeviceFeatures))
{
    CheckRequiredExtensions(physicalDevice, requiredExtensions);
    if (!bufferDeviceAddressEnabled_)
    {
        SPDLOG_WARN("bufferDeviceAddress is not available on this device; buffers drop the "
                    "SHADER_DEVICE_ADDRESS usage and report a null address. Only the compatibility "
                    "renderer, which binds its buffers through descriptors, can run here.");
    }

    const auto queueFamilies = GetEnumerateVector(physicalDevice, vkGetPhysicalDeviceQueueFamilyProperties);


    // Find the graphics queue.
    const auto graphicsFamily = FindQueue(queueFamilies, "graphics", VK_QUEUE_GRAPHICS_BIT, 0, 1);

    // USE SPARSE BINDING AS THREAD LOAD QUEUE
    // On MoltenVK, the total queue count is 1, cannot create more than 1 queue.
#if __APPLE__
    const auto transferFamily = graphicsFamily;
#else
#if ANDROID
    //const auto transferFamily = graphicsFamily;
    auto transferFamily = std::find_if(queueFamilies.begin(), queueFamilies.end(), [](const VkQueueFamilyProperties& queueFamily)
    {
        return queueFamily.queueCount >= 1 &&
            (queueFamily.queueFlags & VK_QUEUE_SPARSE_BINDING_BIT) &&
            !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT);
    });
#else
    auto transferFamily = std::find_if(queueFamilies.begin(), queueFamilies.end(), [](const VkQueueFamilyProperties& queueFamily)
    {
        return queueFamily.queueCount >= 1 &&
            (queueFamily.queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT);
    });
#endif
    if (transferFamily == queueFamilies.end())
    {
        SPDLOG_INFO("No dedicated transfer queue found; using graphics queue for transfers");
        transferFamily = graphicsFamily;
    }
#endif
    
    //Commented out for Macos compatibility, and this queue is not in use actually
    //const auto computeFamily = FindQueue(queueFamilies, "compute", VK_QUEUE_COMPUTE_BIT, VK_QUEUE_GRAPHICS_BIT);
    
    // Find the presentation queue (usually the same as graphics queue).
    const auto presentFamily = surface.Instance().Window().IsHeadless()
        ? graphicsFamily
        : std::find_if(queueFamilies.begin(), queueFamilies.end(), [&](const VkQueueFamilyProperties& queueFamily)
          {
              VkBool32 presentSupport = false;
              const uint32_t i = static_cast<uint32_t>(&queueFamily - queueFamilies.data());
              vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface.Handle(), &presentSupport);
              return queueFamily.queueCount > 0 && presentSupport;
          });

    if (presentFamily == queueFamilies.end())
    {
        Throw(std::runtime_error(
            "The selected graphics device cannot present to a window.\n\n"
            "This usually means the display is driven by a different GPU than the one in use. "
            "Select the other device with --gpu <index>, or plug the display into the card "
            "running the renderer."));
    }

    graphicsFamilyIndex_ = static_cast<uint32_t>(graphicsFamily - queueFamilies.begin());
    computeFamilyIndex_ = graphicsFamilyIndex_;
    presentFamilyIndex_ = static_cast<uint32_t>(presentFamily - queueFamilies.begin());
    transferFamilyIndex_ = static_cast<uint32_t>(transferFamily - queueFamilies.begin());

    // Queues can be the same
    std::set<uint32_t> uniqueQueueFamilies =
    {
        graphicsFamilyIndex_,
        //computeFamilyIndex_,
        presentFamilyIndex_,
        transferFamilyIndex_
    };

    // Extra queue families requested by modules (e.g. NextRemote video encode).
    for (IDeviceCreationAugmenter* augmenter : DeviceCreationAugmenters())
    {
        const uint32_t extraFamily = augmenter->AdditionalQueueFamily(physicalDevice);
        if (extraFamily != UINT32_MAX)
        {
            uniqueQueueFamilies.insert(extraFamily);
            extraQueues_[extraFamily] = VK_NULL_HANDLE;
        }
    }

    std::map<uint32_t, uint32_t> requestedQueueCounts;
    for (uint32_t queueFamilyIndex : uniqueQueueFamilies)
    {
        requestedQueueCounts[queueFamilyIndex] = 1;
    }
    for (IDeviceCreationAugmenter* augmenter : DeviceCreationAugmenters())
    {
        augmenter->AugmentQueueRequests(physicalDevice, surface.Handle(), requestedQueueCounts);
    }
    for (auto& [queueFamilyIndex, queueCount] : requestedQueueCounts)
    {
        if (queueFamilyIndex >= queueFamilies.size())
        {
            Throw(std::runtime_error(fmt::format("module requested invalid queue family {}", queueFamilyIndex)));
        }
        queueCount = std::clamp(queueCount, 1u, queueFamilies[queueFamilyIndex].queueCount);
        uniqueQueueFamilies.insert(queueFamilyIndex);
    }

    // Create queues
    uint32_t maxRequestedQueueCount = 1;
    for (const auto& [queueFamilyIndex, queueCount] : requestedQueueCounts)
    {
        maxRequestedQueueCount = std::max(maxRequestedQueueCount, queueCount);
    }
    std::vector<float> queuePriority(maxRequestedQueueCount, 1.0f);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;

    for (uint32_t queueFamilyIndex : uniqueQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueCreateInfo = {};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamilyIndex;
        queueCreateInfo.queueCount = requestedQueueCounts[queueFamilyIndex];
        queueCreateInfo.pQueuePriorities = queuePriority.data();

        queueCreateInfos.push_back(queueCreateInfo);
    }

    // Create device
    VkDeviceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = nextDeviceFeatures;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledLayerCount = static_cast<uint32_t>(surface_.Instance().ValidationLayers().size());
    createInfo.ppEnabledLayerNames = surface_.Instance().ValidationLayers().data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(requiredExtensions.size());
    createInfo.ppEnabledExtensionNames = requiredExtensions.data();

    Check(Interposer().CreateDevice(physicalDevice, &createInfo, nullptr, &device_),
        "create logical device");

    debugUtils_.SetDevice(device_);

    vkGetDeviceQueue(device_, graphicsFamilyIndex_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, computeFamilyIndex_, 0, &computeQueue_);
    vkGetDeviceQueue(device_, presentFamilyIndex_, 0, &presentQueue_);
    vkGetDeviceQueue(device_, transferFamilyIndex_, 0, &transferQueue_);
    for (auto& [familyIndex, queue] : extraQueues_)
    {
        vkGetDeviceQueue(device_, familyIndex, 0, &queue);
    }
    for (const auto& [familyIndex, queueCount] : requestedQueueCounts)
    {
        auto& queues = createdQueues_[familyIndex];
        queues.resize(queueCount);
        for (uint32_t queueIndex = 0; queueIndex < queueCount; ++queueIndex)
        {
            vkGetDeviceQueue(device_, familyIndex, queueIndex, &queues[queueIndex]);
        }
    }

    vkGetPhysicalDeviceProperties(PhysicalDevice(), &deviceProp_);
    CreatePipelineCache(device_, deviceProp_, pipelineCache_);
    
    deviceProcedures_.reset(new DeviceProcedures(*this, true, true));
    memoryAllocator_.reset(new MemoryAllocator(*this));
}

Device::~Device()
{
    if (device_ != nullptr)
    {
        PersistPipelineCache();
        memoryAllocator_.reset();
        deviceProcedures_.reset();
        if (pipelineCache_ != VK_NULL_HANDLE)
        {
            vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
            pipelineCache_ = VK_NULL_HANDLE;
        }
        Interposer().DestroyDevice(device_, nullptr);
        device_ = nullptr;
    }
}

void Device::PersistPipelineCache() const
{
    if (device_ == VK_NULL_HANDLE || pipelineCache_ == VK_NULL_HANDLE)
    {
        return;
    }

    size_t dataSize = 0;
    VkResult result = vkGetPipelineCacheData(device_, pipelineCache_, &dataSize, nullptr);
    if (result != VK_SUCCESS || dataSize > MaxPipelineCacheBytes)
    {
        SPDLOG_WARN("Vulkan pipeline cache: query data size failed ({})", static_cast<int>(result));
        return;
    }

    std::vector<uint8_t> data(dataSize);
    result = vkGetPipelineCacheData(device_, pipelineCache_, &dataSize, data.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        SPDLOG_WARN("Vulkan pipeline cache: read data failed ({})", static_cast<int>(result));
        return;
    }
    data.resize(dataSize);

    PipelineCacheFileHeader header{};
    header.vendorId = deviceProp_.vendorID;
    header.deviceId = deviceProp_.deviceID;
    header.driverVersion = deviceProp_.driverVersion;
    std::copy_n(deviceProp_.pipelineCacheUUID, VK_UUID_SIZE, header.pipelineCacheUuid.begin());
    header.dataSize = data.size();

    const std::filesystem::path path = GetPipelineCachePath(deviceProp_);
    const std::filesystem::path temporaryPath = path.string() + ".tmp";
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        SPDLOG_WARN("Vulkan pipeline cache: cannot write {}", temporaryPath.string());
        return;
    }
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    output.close();
    if (!output)
    {
        SPDLOG_WARN("Vulkan pipeline cache: write failed for {}", temporaryPath.string());
        return;
    }

    std::error_code error;
    std::filesystem::rename(temporaryPath, path, error);
    if (error)
    {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temporaryPath, path, error);
    }
    if (error)
    {
        SPDLOG_WARN("Vulkan pipeline cache: cannot publish {} ({})", path.string(), error.message());
        return;
    }
    SPDLOG_INFO("Vulkan pipeline cache: saved {} KiB to {}", data.size() / 1024, path.string());
}

void Device::RecordPipelineCreated(const char* label) const
{
    const uint32_t number = pipelineCreationCount_.fetch_add(1) + 1;
    SPDLOG_DEBUG("Vulkan pipeline warm-up: created pipeline #{} ({})", number,
                 label != nullptr ? label : "unnamed");
}

MemoryStatsSnapshot Device::CaptureMemoryStats(bool includeDetails) const
{
    return memoryAllocator_->CaptureStats(includeDetails);
}

void Device::WaitIdle() const
{
    Check(Interposer().DeviceWaitIdle(device_),
        "wait for device idle");
}

VkQueue Device::QueueForFamilyIndex(uint32_t queueFamilyIndex) const
{
    if (queueFamilyIndex == graphicsFamilyIndex_)
    {
        return graphicsQueue_;
    }
    if (queueFamilyIndex == computeFamilyIndex_)
    {
        return computeQueue_;
    }
    if (queueFamilyIndex == presentFamilyIndex_)
    {
        return presentQueue_;
    }
    if (queueFamilyIndex == static_cast<uint32_t>(transferFamilyIndex_))
    {
        return transferQueue_;
    }
    if (const auto extraQueue = extraQueues_.find(queueFamilyIndex); extraQueue != extraQueues_.end())
    {
        return extraQueue->second;
    }
    Throw(std::runtime_error(fmt::format("queue family {} is not available on this device", queueFamilyIndex)));
}

VkQueue Device::QueueForFamilyIndex(uint32_t queueFamilyIndex, uint32_t queueIndex) const
{
    const auto family = createdQueues_.find(queueFamilyIndex);
    if (family == createdQueues_.end() || queueIndex >= family->second.size())
    {
        Throw(std::runtime_error(fmt::format("queue family {} index {} is not available on this device",
                                             queueFamilyIndex, queueIndex)));
    }
    return family->second[queueIndex];
}

uint32_t Device::CreatedQueueCount(uint32_t queueFamilyIndex) const
{
    const auto family = createdQueues_.find(queueFamilyIndex);
    return family != createdQueues_.end() ? static_cast<uint32_t>(family->second.size()) : 0;
}

void Device::CheckRequiredExtensions(VkPhysicalDevice physicalDevice, const std::vector<const char*>& requiredExtensions) const
{
    const auto availableExtensions = GetEnumerateVector(physicalDevice, static_cast<const char*>(nullptr), vkEnumerateDeviceExtensionProperties);
    std::set<std::string> required(requiredExtensions.begin(), requiredExtensions.end());

    for (const auto& extension : availableExtensions) 
    {
        required.erase(extension.extensionName);
    }

    if (!required.empty())
    {
        bool first = true;
        std::string extensions;

        for (const auto& extension : required)
        {
            if (!first)
            {
                extensions += ", ";
            }

            extensions += extension;
            first = false;
        }

        // This message reaches end users through the startup error dialog, so it names
        // the device and the exact missing capability rather than only the extension.
        VkPhysicalDeviceProperties properties = {};
        vkGetPhysicalDeviceProperties(physicalDevice, &properties);

        Throw(std::runtime_error(fmt::format(
            "Your graphics device does not support everything this renderer needs.\n\n"
            "Device: {}\n"
            "Driver: {}.{}.{} (Vulkan {}.{}.{})\n"
            "Missing Vulkan extensions: {}\n\n"
            "Update to the latest driver from your GPU vendor. If the device is older than "
            "the Vulkan 1.3 baseline, it cannot run this build.",
            properties.deviceName,
            VK_VERSION_MAJOR(properties.driverVersion), VK_VERSION_MINOR(properties.driverVersion),
            VK_VERSION_PATCH(properties.driverVersion),
            VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
            VK_VERSION_PATCH(properties.apiVersion),
            extensions)));
    }
}

}
