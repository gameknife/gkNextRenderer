#pragma once

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
                             std::function<void()> onCompleted = {});
};
