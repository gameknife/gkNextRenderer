#include "ImageView.hpp"
#include "Device.hpp"

namespace Vulkan {

ImageView::ImageView(const class Device& device, const VkImage image, const VkFormat format, const VkImageAspectFlags aspectFlags, const uint32_t miplevel) :
	device_(device)
{
	VkImageViewCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	createInfo.image = image;
	createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	createInfo.format = format;
	createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
	createInfo.subresourceRange.aspectMask = aspectFlags;
	createInfo.subresourceRange.baseMipLevel = 0;
	createInfo.subresourceRange.levelCount = miplevel;
	createInfo.subresourceRange.baseArrayLayer = 0;
	createInfo.subresourceRange.layerCount = 1;

	Check(vkCreateImageView(device_.Handle(), &createInfo, nullptr, &imageView_),
		"create image view");
}

ImageView::~ImageView()
{
	if (imageView_ != nullptr)
	{
		vkDestroyImageView(device_.Handle(), imageView_, nullptr);
		imageView_ = nullptr;
	}
}

}