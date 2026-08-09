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
        uint32_t supportedProfileMask = 0;
        VkExtent2D minExtent{0, 0};
        VkExtent2D maxExtent{0, 0};
        VkExtent2D pictureAccessGranularity{1, 1};
        uint32_t maxDpbSlots = 0;
        uint32_t maxActiveReferencePictures = 0;
        uint64_t maxBitrate = 0;
        uint32_t maxQualityLevels = 0;
        VkDeviceSize minBitstreamBufferOffsetAlignment = 1;
        VkDeviceSize minBitstreamBufferSizeAlignment = 1;
        VkVideoEncodeRateControlModeFlagsKHR rateControlModes = 0;
        VkVideoEncodeFeedbackFlagsKHR supportedEncodeFeedbackFlags = 0;
        int32_t minQp = 0;
        int32_t maxQp = 51;
        uint32_t preferredQualityLevel = UINT32_MAX;
        VkVideoEncodeRateControlModeFlagBitsKHR preferredRateControlMode =
            VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR;
        VkVideoEncodeH264RateControlFlagsKHR preferredRateControlFlags = 0;
        uint32_t preferredGopFrameCount = 0;
        uint32_t preferredIdrPeriod = 0;
        VkVideoEncodeH264QpKHR preferredConstantQp{26, 28, 30};
        uint32_t preferredMaxL0ReferenceCount = 1;
        bool preferredEntropyCodingMode = false;

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
