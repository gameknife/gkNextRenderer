#pragma once

#include "Vulkan/Image.hpp"
#include <memory>

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
		~TextureImage();

		const Vulkan::ImageView& ImageView() const { return *imageView_; }
		const Vulkan::Sampler& Sampler() const { return *sampler_; }
		void MainThreadPostLoading(Vulkan::CommandPool& commandPool);

	private:

		std::unique_ptr<Vulkan::Image> image_;
		std::unique_ptr<Vulkan::DeviceMemory> imageMemory_;
		std::unique_ptr<Vulkan::ImageView> imageView_;
		std::unique_ptr<Vulkan::Sampler> sampler_;
	};

}
