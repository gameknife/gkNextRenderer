#pragma once

// Internal declarations shared between UserInterface.cpp and
// UserInterface.ViewportBackend.cpp. Not engine public API.

#include "Engine/Vulkan/DebugUtilities.hpp"

namespace Vulkan
{
    class Device;
}

namespace NextUI
{
    // Builds the bindless UI graphics pipeline used by both the main swapchain
    // pass and the per-viewport platform windows (UserInterface.cpp).
    VkPipeline CreateUiGraphicsPipeline(const Vulkan::Device& device, VkPipelineLayout pipelineLayout,
                                        VkRenderPass renderPass);
}
