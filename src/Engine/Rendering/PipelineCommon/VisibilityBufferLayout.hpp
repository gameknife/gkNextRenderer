#pragma once

#include "Engine/Assets/GPU/UniformBuffer.hpp"

#include <array>

namespace Vulkan::PipelineCommon
{
    struct FVisibilityPlaneDesc
    {
        uint32_t storageSlot;
        uint32_t drawSlot;
        VkFormat format;
    };

    inline constexpr std::array<FVisibilityPlaneDesc, 2> kVisibilityPlanes = {{
        {Assets::Bindless::RT_VISIBILITY_INSTANCE,
         Assets::Bindless::RT_VISIBILITY_INSTANCE_DRAW,
         VK_FORMAT_R32_UINT},
        {Assets::Bindless::RT_VISIBILITY_TRIANGLE,
         Assets::Bindless::RT_VISIBILITY_TRIANGLE_DRAW,
         VK_FORMAT_R16_UINT},
    }};
}
