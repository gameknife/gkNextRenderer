#pragma once

#include <string>

namespace Vulkan
{
	class VulkanBaseRenderer;
}

namespace ScreenShot
{
	void SaveSwapChainToFileFast(Vulkan::VulkanBaseRenderer* renderer_, const std::string& filePathWithoutExtension, int x, int y, int width, int height);
	void SaveSwapChainToFile(Vulkan::VulkanBaseRenderer* renderer_, const std::string& filePathWithoutExtension, int x, int y, int width, int height);
};