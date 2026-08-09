#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/VideoEncodeDeviceAugmenter.hpp"

#include "Engine/Vulkan/DeviceCreationAugmenter.hpp"

namespace Modules::NextRemote
{
    namespace
    {
        class FVideoEncodeAugmenter final : public Vulkan::IDeviceCreationAugmenter
        {
        public:
            Vulkan::FVulkanVideoCaps caps{};

            void* OnPhysicalDeviceSelected(VkInstance instance,
                                           VkPhysicalDevice physicalDevice,
                                           std::vector<const char*>& requiredExtensions,
                                           void* featureChain) override
            {
                caps = Vulkan::FVulkanVideoCaps::Probe(instance, physicalDevice);
                caps.LogSummary();
                if (!caps.Usable())
                {
                    SPDLOG_WARN("RemotePlay: Vulkan Video H.264 encode is not usable on this device; remote mode "
                                "will be unavailable");
                    return featureChain;
                }

                requiredExtensions.insert(requiredExtensions.end(),
                    {
                        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                        VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
                        VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
                        VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME,
                    });
                if (caps.maintenance1Present)
                {
                    requiredExtensions.push_back(VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME);
                }

                synchronization2Features_ = {};
                synchronization2Features_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
                synchronization2Features_.pNext = featureChain;
                synchronization2Features_.synchronization2 = true;
                return &synchronization2Features_;
            }

            uint32_t AdditionalQueueFamily(VkPhysicalDevice physicalDevice) override
            {
                return caps.Usable() ? caps.encodeQueueFamily : UINT32_MAX;
            }

        private:
            VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2Features_{};
        };

        FVideoEncodeAugmenter& Augmenter()
        {
            static FVideoEncodeAugmenter augmenter;
            return augmenter;
        }
    }

    void RegisterVideoEncodeAugmenter()
    {
        static bool registered = false;
        if (!registered)
        {
            Vulkan::RegisterDeviceCreationAugmenter(&Augmenter());
            registered = true;
        }
    }

    const Vulkan::FVulkanVideoCaps& ProbedVideoCaps()
    {
        return Augmenter().caps;
    }
}
