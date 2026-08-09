#pragma once

#include "Engine/Common/CoreMinimal.hpp"

#include <vulkan/vulkan.h>

namespace Vulkan
{
    class Device;
}

namespace NextUI
{
    class FImGuiVulkanRenderer final
    {
    public:
        VkPipeline CreateGraphicsPipeline(const Vulkan::Device& device,
                                          VkPipelineLayout pipelineLayout,
                                          VkRenderPass renderPass) const;
    };
}
