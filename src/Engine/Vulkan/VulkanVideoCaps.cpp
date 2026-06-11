#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/VulkanVideoCaps.hpp"



namespace Runtime::Remote
{
    namespace
    {
        bool HasExtension(const std::vector<VkExtensionProperties>& available, const char* name)
        {
            for (const auto& extension : available)
            {
                if (std::strcmp(extension.extensionName, name) == 0)
                {
                    return true;
                }
            }
            return false;
        }

        const char* ProfileIdcName(int32_t idc)
        {
            switch (idc)
            {
            case STD_VIDEO_H264_PROFILE_IDC_BASELINE: return "ConstrainedBaseline";
            case STD_VIDEO_H264_PROFILE_IDC_MAIN: return "Main";
            case STD_VIDEO_H264_PROFILE_IDC_HIGH: return "High";
            default: return "none";
            }
        }

        VkVideoProfileInfoKHR MakeProfile(VkVideoEncodeH264ProfileInfoKHR& h264Profile,
                                          StdVideoH264ProfileIdc profileIdc)
        {
            h264Profile = {};
            h264Profile.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR;
            h264Profile.stdProfileIdc = profileIdc;

            VkVideoProfileInfoKHR profile{};
            profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
            profile.pNext = &h264Profile;
            profile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR;
            profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
            profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
            profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
            return profile;
        }
    }

    uint32_t FVulkanVideoCaps::FindEncodeH264QueueFamily(VkPhysicalDevice physicalDevice)
    {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &count, nullptr);
        if (count == 0)
        {
            return UINT32_MAX;
        }

        std::vector<VkQueueFamilyVideoPropertiesKHR> videoProps(count);
        std::vector<VkQueueFamilyProperties2> props(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            videoProps[i] = {};
            videoProps[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
            props[i] = {};
            props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
            props[i].pNext = &videoProps[i];
        }
        vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &count, props.data());

        for (uint32_t i = 0; i < count; ++i)
        {
            if ((props[i].queueFamilyProperties.queueFlags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) != 0 &&
                (videoProps[i].videoCodecOperations & VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR) != 0)
            {
                return i;
            }
        }
        return UINT32_MAX;
    }

    FVulkanVideoCaps FVulkanVideoCaps::Probe(VkInstance instance, VkPhysicalDevice physicalDevice)
    {
        FVulkanVideoCaps caps;

        uint32_t extensionCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
        std::vector<VkExtensionProperties> extensions(extensionCount);
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());

        caps.extensionsPresent = HasExtension(extensions, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) &&
                                 HasExtension(extensions, VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME) &&
                                 HasExtension(extensions, VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME) &&
                                 HasExtension(extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
        caps.maintenance1Present = HasExtension(extensions, VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME);
        if (!caps.extensionsPresent)
        {
            return caps;
        }

        caps.encodeQueueFamily = FindEncodeH264QueueFamily(physicalDevice);
        if (caps.encodeQueueFamily == UINT32_MAX)
        {
            return caps;
        }

        const auto pfnGetVideoCapabilities = reinterpret_cast<PFN_vkGetPhysicalDeviceVideoCapabilitiesKHR>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceVideoCapabilitiesKHR"));
        const auto pfnGetVideoFormatProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR>(
            vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceVideoFormatPropertiesKHR"));
        if (!pfnGetVideoCapabilities || !pfnGetVideoFormatProperties)
        {
            return caps;
        }

        // Prefer ConstrainedBaseline (matches the default 42e01f SDP fmtp), then Main, then High.
        constexpr std::array<StdVideoH264ProfileIdc, 3> profileCandidates = {
            STD_VIDEO_H264_PROFILE_IDC_BASELINE,
            STD_VIDEO_H264_PROFILE_IDC_MAIN,
            STD_VIDEO_H264_PROFILE_IDC_HIGH,
        };

        VkVideoEncodeH264ProfileInfoKHR h264Profile{};
        VkVideoProfileInfoKHR profile{};
        for (const StdVideoH264ProfileIdc candidate : profileCandidates)
        {
            profile = MakeProfile(h264Profile, candidate);

            VkVideoEncodeH264CapabilitiesKHR h264Caps{};
            h264Caps.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR;
            VkVideoEncodeCapabilitiesKHR encodeCaps{};
            encodeCaps.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR;
            encodeCaps.pNext = &h264Caps;
            VkVideoCapabilitiesKHR videoCaps{};
            videoCaps.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
            videoCaps.pNext = &encodeCaps;

            if (pfnGetVideoCapabilities(physicalDevice, &profile, &videoCaps) != VK_SUCCESS)
            {
                continue;
            }

            caps.h264Supported = true;
            caps.profileIdc = candidate;
            caps.minExtent = videoCaps.minCodedExtent;
            caps.maxExtent = videoCaps.maxCodedExtent;
            caps.pictureAccessGranularity = videoCaps.pictureAccessGranularity;
            caps.maxDpbSlots = videoCaps.maxDpbSlots;
            caps.maxActiveReferencePictures = videoCaps.maxActiveReferencePictures;
            caps.minBitstreamBufferOffsetAlignment = videoCaps.minBitstreamBufferOffsetAlignment;
            caps.minBitstreamBufferSizeAlignment = videoCaps.minBitstreamBufferSizeAlignment;
            break;
        }
        if (!caps.h264Supported)
        {
            return caps;
        }

        // ENCODE_SRC format support for NV12, with and without STORAGE for the zero-copy path.
        const auto queryNv12 = [&](VkImageUsageFlags usage) -> bool
        {
            VkVideoProfileListInfoKHR profileList{};
            profileList.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
            profileList.profileCount = 1;
            profileList.pProfiles = &profile;

            VkPhysicalDeviceVideoFormatInfoKHR formatInfo{};
            formatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR;
            formatInfo.pNext = &profileList;
            formatInfo.imageUsage = usage;

            uint32_t formatCount = 0;
            if (pfnGetVideoFormatProperties(physicalDevice, &formatInfo, &formatCount, nullptr) != VK_SUCCESS ||
                formatCount == 0)
            {
                return false;
            }
            std::vector<VkVideoFormatPropertiesKHR> formats(formatCount);
            for (auto& format : formats)
            {
                format = {};
                format.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
            }
            if (pfnGetVideoFormatProperties(physicalDevice, &formatInfo, &formatCount, formats.data()) != VK_SUCCESS)
            {
                return false;
            }
            for (const auto& format : formats)
            {
                if (format.format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM)
                {
                    return true;
                }
            }
            return false;
        };

        caps.nv12EncodeSrc = queryNv12(VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR);
        caps.nv12EncodeSrcStorage =
            queryNv12(VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_STORAGE_BIT);

        return caps;
    }

    void FVulkanVideoCaps::LogSummary() const
    {
        if (!extensionsPresent)
        {
            SPDLOG_INFO("RemotePlay: Vulkan Video encode unavailable (missing device extensions)");
            return;
        }
        if (encodeQueueFamily == UINT32_MAX)
        {
            SPDLOG_INFO("RemotePlay: Vulkan Video encode unavailable (no H.264 encode queue family)");
            return;
        }
        if (!h264Supported)
        {
            SPDLOG_INFO("RemotePlay: Vulkan Video encode unavailable (H.264 profile not supported)");
            return;
        }
        SPDLOG_INFO(
            "RemotePlay: Vulkan Video H.264 encode available: queueFamily={} profile={} coded={}x{}..{}x{} "
            "granularity={}x{} dpb={} refs={} nv12EncodeSrc={} nv12+storage={} maintenance1={}",
            encodeQueueFamily, ProfileIdcName(profileIdc), minExtent.width, minExtent.height, maxExtent.width,
            maxExtent.height, pictureAccessGranularity.width, pictureAccessGranularity.height, maxDpbSlots,
            maxActiveReferencePictures, nv12EncodeSrc, nv12EncodeSrcStorage, maintenance1Present);
    }
}
