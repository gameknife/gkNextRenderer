#pragma once

#include "Engine/Runtime/Interface/ScreenShotService.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <functional>
#include <string>

namespace Runtime::ScreenShot
{
    void SaveSwapChainToFile(Vulkan::VulkanBaseRenderer* renderer_,
                             const std::string& filePathWithoutExtension,
                             int x,
                             int y,
                             int width,
                             int height,
                             EFileFormat fileFormat = EFileFormat::Automatic,
                             // Synchronous captures encode and close the image before returning.
                             bool synchronous = false,
                             std::function<void()> onCompleted = {},
                             std::function<void()> onReadbackCompleted = {});
};
