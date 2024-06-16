#pragma once

#include <memory>

#include "Vulkan.hpp"
#include "DeviceMemory.hpp"

namespace Vulkan
{
	class ImageView;
	class Image;
	class Buffer;
	class CommandPool;
	class Device;

	class RenderImage final
	{
	public:

		RenderImage(const RenderImage&) = delete;
		RenderImage& operator = (const RenderImage&) = delete;
		RenderImage& operator = (RenderImage&&) = delete;

		RenderImage(const Device& device,VkExtent2D extent, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, const char* debugName = nullptr);
		~RenderImage();

		const Image& GetImage() const { return *image_; }
		const ImageView& GetImageView() const { return *imageView_; }
		void InsertBarrier(VkCommandBuffer commandBuffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout) const;
	
	private:
		std::unique_ptr<Image> image_;
		std::unique_ptr<DeviceMemory> imageMemory_;
		std::unique_ptr<ImageView> imageView_;
		
	};

}
