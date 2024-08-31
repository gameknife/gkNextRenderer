#pragma once

#include <memory>

#include "Vulkan.hpp"
#include "DeviceMemory.hpp"
#include "Sampler.hpp"

#if WIN32 && !defined(__MINGW32__)
#define ExtHandle HANDLE
#else
#define ExtHandle int
#endif
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

		RenderImage(const Device& device,VkExtent2D extent, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, bool external = false, const char* debugName = nullptr);
		~RenderImage();

		const Image& GetImage() const { return *image_; }
		const ImageView& GetImageView() const { return *imageView_; }
		const Vulkan::Sampler& Sampler() const { return *sampler_; }
		void InsertBarrier(VkCommandBuffer commandBuffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout) const;
		ExtHandle GetExternalHandle() const;
	private:
		std::unique_ptr<Image> image_;
		std::unique_ptr<DeviceMemory> imageMemory_;
		std::unique_ptr<ImageView> imageView_;
		std::unique_ptr<Vulkan::Sampler> sampler_;
		
	};

}
