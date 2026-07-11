#pragma once

#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include <memory>
#include <vector>

namespace Vulkan
{
	enum class ESwapChainOutputMode : uint32_t
	{
		SDR = 0,
		HDR10_ST2084 = 1,
		ExtendedSrgbLinear = 2,
	};

	class SwapChain final
	{
	public:

		VULKAN_NON_COPIABLE(SwapChain)

		SwapChain(const Device& device, VkPresentModeKHR presentMode, bool forceSDR);
		~SwapChain();

		VkPhysicalDevice PhysicalDevice() const { return physicalDevice_; }
		const class Device& Device() const { return device_; }
		uint32_t MinImageCount() const { return minImageCount_; }
		const std::vector<VkImage>& Images() const { return images_; }
		const std::vector<std::unique_ptr<ImageView>>& ImageViews() const { return imageViews_; }
		const VkExtent2D& Extent() const { return extent_; }
		const VkExtent2D& RenderExtent() const { return renderExtent_; }
		const VkOffset2D& RenderOffset() const { return renderOffset_; }
		const VkExtent2D& OutputExtent() const { return outputExtent_; }
		const VkOffset2D& OutputOffset() const { return outputOffset_; }
		
		VkFormat Format() const { return format_; }
		VkColorSpaceKHR ColorSpace() const { return colorSpace_; }
		VkPresentModeKHR PresentMode() const { return presentMode_; }
		ESwapChainOutputMode OutputMode() const { return outputMode_; }
		uint32_t HDROutputMode() const { return static_cast<uint32_t>(outputMode_); }
		bool IsHDR() const { return outputMode_ != ESwapChainOutputMode::SDR; }
		VkImageUsageFlags ImageUsage() const { return imageUsage_; }
		bool SupportsUsage(VkImageUsageFlagBits usage) const { return (imageUsage_ & usage) != 0; }

		void UpdateRenderViewport( int32_t x, int32_t y, uint32_t width, uint32_t height) const;
		void UpdateOutputViewport( int32_t x, int32_t y, uint32_t width, uint32_t height) const;
		
	private:

		struct SupportDetails
		{
			VkSurfaceCapabilitiesKHR Capabilities{};
			std::vector<VkSurfaceFormatKHR> Formats;
			std::vector<VkPresentModeKHR> PresentModes;
		};

		static SupportDetails QuerySwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats, bool forceSDR);
		static VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes, VkPresentModeKHR presentMode);
		static VkExtent2D ChooseSwapExtent(const Window& window, const VkSurfaceCapabilitiesKHR& capabilities);
		static uint32_t ChooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities);

		const VkPhysicalDevice physicalDevice_;
		const class Device& device_;

		VULKAN_HANDLE(VkSwapchainKHR, swapChain_)

		uint32_t minImageCount_;
		VkPresentModeKHR presentMode_;
		VkFormat format_;
		VkColorSpaceKHR colorSpace_;
		VkImageUsageFlags imageUsage_{};
		VkExtent2D extent_{};
		mutable VkExtent2D renderExtent_{};
		mutable VkOffset2D renderOffset_{};

		mutable VkExtent2D outputExtent_{};
		mutable VkOffset2D outputOffset_{};
		
		std::vector<VkImage> images_;
		std::vector<std::unique_ptr<ImageView>> imageViews_;
		ESwapChainOutputMode outputMode_{ESwapChainOutputMode::SDR};
	};

}
