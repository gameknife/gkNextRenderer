#pragma once

#include "Engine/Vulkan/VulkanFwd.hpp"

#include <functional>
#include <string>

namespace Runtime::ScreenShot
{
    enum class EFileFormat
    {
        Automatic,
        Jpeg,
    };

    void SaveSwapChainToFile(Vulkan::VulkanBaseRenderer* renderer_,
                             const std::string& filePathWithoutExtension,
                             int x,
                             int y,
                             int width,
                             int height,
                             EFileFormat fileFormat = EFileFormat::Automatic,
                             std::function<void()> onCompleted = {},
                             std::function<void()> onReadbackCompleted = {});
};
