#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Vulkan
{
    // Physical-device probe result for VK_KHR_video_encode_h264. Probed before logical device
    // creation; drives the --remote-encoder auto path selection and which device extensions get
    // enabled.
    struct FVulkanVideoCaps
    {
        bool extensionsPresent = false;   // video_queue + video_encode_queue + video_encode_h264 + synchronization2
        bool maintenance1Present = false; // VK_KHR_video_maintenance1 (optional)
        uint32_t encodeQueueFamily = UINT32_MAX;

        bool h264Supported = false;
        int32_t profileIdc = -1;          // chosen StdVideoH264ProfileIdc
        VkExtent2D minExtent{0, 0};
        VkExtent2D maxExtent{0, 0};
        VkExtent2D pictureAccessGranularity{1, 1};
        uint32_t maxDpbSlots = 0;
        uint32_t maxActiveReferencePictures = 0;
        VkDeviceSize minBitstreamBufferOffsetAlignment = 1;
        VkDeviceSize minBitstreamBufferSizeAlignment = 1;

        bool nv12EncodeSrc = false;        // G8_B8R8_2PLANE_420 usable as ENCODE_SRC
        bool nv12EncodeSrcStorage = false; // ... with STORAGE usage as well (zero-copy compute write)

        bool Usable() const
        {
            return extensionsPresent && encodeQueueFamily != UINT32_MAX && h264Supported && nv12EncodeSrc;
        }

        static FVulkanVideoCaps Probe(VkInstance instance, VkPhysicalDevice physicalDevice);
        static uint32_t FindEncodeH264QueueFamily(VkPhysicalDevice physicalDevice);

        void LogSummary() const;
    };
}
