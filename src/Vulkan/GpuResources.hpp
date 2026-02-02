#pragma once

#include "DebugUtilities.hpp"
#include "MemoryAndShader.hpp"
#include "MemoryAndShader.hpp"
#include <memory>

#if WIN32 && !defined(__MINGW32__)
#define ExtHandle HANDLE
#else
#define ExtHandle int
#endif

namespace Vulkan
{
	class CommandPool;
	class Device;

	// ============================================================================
	// Image
	// ============================================================================

	class Image final
	{
	public:

		Image(const Image&) = delete;
		Image& operator = (const Image&) = delete;
		Image& operator = (Image&&) = delete;

		Image(const Device& device, VkExtent2D extent, uint32_t miplevel, VkFormat format);
		Image(const Device& device, VkExtent2D extent, uint32_t miplevel, VkFormat format,VkImageTiling tiling, VkImageUsageFlags usage, bool useForExternal = false);
		Image(Image&& other) noexcept;
		~Image();

		const class Device& Device() const { return device_; }
		VkExtent2D Extent() const { return extent_; }
		VkFormat Format() const { return format_; }

		DeviceMemory AllocateMemory(VkMemoryPropertyFlags properties, bool external = false) const;
		VkMemoryRequirements GetMemoryRequirements() const;

		void TransitionImageLayout(CommandPool& commandPool, VkImageLayout newLayout);
		void CopyFrom(CommandPool& commandPool, const class Buffer& buffer);

		void CopyFromToMipLevel(
			Vulkan::CommandPool& commandPool,
			const class Buffer& buffer,
			uint32_t mipLevel,
			uint32_t mipWidth,
			uint32_t mipHeight);

	private:

		const class Device& device_;
		const VkExtent2D extent_;
		const VkFormat format_;
		VkImageLayout imageLayout_;
		uint32_t mipLevel_;
		bool external_;
		VULKAN_HANDLE(VkImage, image_)
	};

	// ============================================================================
	// ImageView
	// ============================================================================

	class ImageView final
	{
	public:

		VULKAN_NON_COPIABLE(ImageView)

		explicit ImageView(const Device& device, VkImage image, VkFormat format, VkImageAspectFlags aspectFlags, const uint32_t miplevel = 1);
		~ImageView();

		const class Device& Device() const { return device_; }


	private:

		const class Device& device_;

		VULKAN_HANDLE(VkImageView, imageView_)
	};

	// ============================================================================
	// Buffer
	// ============================================================================

	class Buffer final
	{
	public:

		VULKAN_NON_COPIABLE(Buffer)

		Buffer(const Device& device, size_t size, VkBufferUsageFlags usage);
		~Buffer();

		const class Device& Device() const { return device_; }

		DeviceMemory AllocateMemory(VkMemoryPropertyFlags propertyFlags);
		DeviceMemory AllocateMemory(VkMemoryAllocateFlags allocateFlags, VkMemoryPropertyFlags propertyFlags);
		VkMemoryRequirements GetMemoryRequirements() const;
		VkDeviceAddress GetDeviceAddress() const;

		void CopyFrom(CommandPool& commandPool, const Buffer& src, VkDeviceSize size);
		void CopyTo(CommandPool& commandPool, const Buffer& dst, VkDeviceSize size);

	private:

		const class Device& device_;

		VULKAN_HANDLE(VkBuffer, buffer_)
	};

	// ============================================================================
	// DepthBuffer
	// ============================================================================

	class DepthBuffer final
	{
	public:

		VULKAN_NON_COPIABLE(DepthBuffer)

		DepthBuffer(CommandPool& commandPool, VkExtent2D extent);
		~DepthBuffer();

		VkFormat Format() const { return format_; }
		const class Image& GetImage() const { return *image_; }
		const class DeviceMemory& GetImageMemory() const { return *imageMemory_; }
		const class ImageView& ImageView() const { return *imageView_; }

		static bool HasStencilComponent(const VkFormat format)
		{
			return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
		}

	private:

		const VkFormat format_;
		std::unique_ptr<Image> image_;
		std::unique_ptr<DeviceMemory> imageMemory_;
		std::unique_ptr<class ImageView> imageView_;
	};

	// ============================================================================
	// RenderImage
	// ============================================================================

	class RenderImage final
	{
	public:

		RenderImage(const RenderImage&) = delete;
		RenderImage& operator = (const RenderImage&) = delete;
		RenderImage& operator = (RenderImage&&) = delete;

		RenderImage(const Device& device,VkExtent2D extent, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, bool external = false, const char* debugName = nullptr);
		~RenderImage();

		const Image& GetImage() const { return *image_; }
		const DeviceMemory& GetImageMemory() const { return *imageMemory_; }
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
