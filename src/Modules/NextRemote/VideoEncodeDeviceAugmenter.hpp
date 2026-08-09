#pragma once

#include "Modules/NextRemote/VulkanVideoCaps.hpp"

namespace Modules::NextRemote
{
    // Registers the device-creation augmenter that probes Vulkan Video H.264 encode
    // caps and enables the encode extensions + queue. Call before device creation
    // (CreateRemoteServer does this in remote mode).
    void RegisterVideoEncodeAugmenter();

    // Caps probed during device creation; default-constructed (unusable) before that.
    const Vulkan::FVulkanVideoCaps& ProbedVideoCaps();
}
