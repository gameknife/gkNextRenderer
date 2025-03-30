#pragma once

#include "Vulkan/Image.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Vulkan
{
	class CommandPool;
	class DeviceMemory;
	class Image;
	class ImageView;
	class Sampler;
}

namespace Assets
{
	class Texture;
	
	class TextureImage final
	{
	public:

		TextureImage(const TextureImage&) = delete;
		TextureImage(TextureImage&&) = delete;
		TextureImage& operator = (const TextureImage&) = delete;
		TextureImage& operator = (TextureImage&&) = delete;

		TextureImage(Vulkan::CommandPool& commandPool, size_t width, size_t height, uint32_t miplevel, VkFormat format, const unsigned char* data, uint32_t size);
		// Add to TextureImage.hpp in the public section
		TextureImage(
		    Vulkan::CommandPool& commandPool, 
		    size_t width, 
		    size_t height, 
		    uint32_t mipLevels, 
		    VkFormat format, 
		    const unsigned char* baseData, 
		    uint32_t baseSize,
		    const std::vector<float*>& mipLevelData, 
		    const std::vector<int>& mipWidths, 
		    const std::vector<int>& mipHeights);
		~TextureImage();

		const Vulkan::ImageView& ImageView() const { return *imageView_; }
		const Vulkan::Sampler& Sampler() const { return *sampler_; }
		void MainThreadPostLoading(Vulkan::CommandPool& commandPool);

		void UpdateDataMainThread(
			Vulkan::CommandPool& commandPool,
			uint32_t startX,
			uint32_t startY,
			uint32_t width,
			uint32_t height,
			const unsigned char* data,
			uint32_t size);

		void SetDebugName(const std::string& name);

	private:

		std::unique_ptr<Vulkan::Image> image_;
		std::unique_ptr<Vulkan::DeviceMemory> imageMemory_;
		std::unique_ptr<Vulkan::ImageView> imageView_;
		std::unique_ptr<Vulkan::Sampler> sampler_;
	};

}
