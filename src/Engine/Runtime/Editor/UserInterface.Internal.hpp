#pragma once

// Internal declarations used by UserInterface.cpp. Not engine public API.

#include "Engine/Vulkan/DebugUtilities.hpp"

namespace Vulkan
{
    class Device;
}

namespace NextUI
{
    // Builds the bindless UI graphics pipeline used by the main swapchain pass.
    VkPipeline CreateUiGraphicsPipeline(const Vulkan::Device& device, VkPipelineLayout pipelineLayout,
                                        VkRenderPass renderPass);
}
