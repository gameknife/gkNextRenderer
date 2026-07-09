#include "Engine/Vulkan/DeviceCreationAugmenter.hpp"

namespace Vulkan
{
    namespace
    {
        std::vector<IDeviceCreationAugmenter*>& AugmenterList()
        {
            static std::vector<IDeviceCreationAugmenter*> augmenters;
            return augmenters;
        }
    }

    void RegisterDeviceCreationAugmenter(IDeviceCreationAugmenter* augmenter)
    {
        AugmenterList().push_back(augmenter);
    }

    const std::vector<IDeviceCreationAugmenter*>& DeviceCreationAugmenters()
    {
        return AugmenterList();
    }
}
