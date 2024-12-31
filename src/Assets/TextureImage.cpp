#include "TextureImage.hpp"
#include "Texture.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/CommandPool.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/Sampler.hpp"
#include <cstring>

namespace Assets {

TextureImage::TextureImage(Vulkan::CommandPool& commandPool, size_t width, size_t height, uint32_t miplevel, VkFormat format, const unsigned char* data, uint32_t size)
{
	// Create a host staging buffer and copy the image into it.
	const VkDeviceSize imageSize = size;
	const auto& device = commandPool.Device();

	auto stagingBuffer = std::make_unique<Vulkan::Buffer>(device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	auto stagingBufferMemory = stagingBuffer->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

	const auto stagingData = stagingBufferMemory.Map(0, imageSize);
	std::memcpy(stagingData, data, imageSize);
	stagingBufferMemory.Unmap();
	
	// Create the device side image, memory, view and sampler.
	image_.reset(new Vulkan::Image(device, VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) }, miplevel, format));
	imageMemory_.reset(new Vulkan::DeviceMemory(image_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	imageView_.reset(new Vulkan::ImageView(device, image_->Handle(), image_->Format(), VK_IMAGE_ASPECT_COLOR_BIT));
	sampler_.reset(new Vulkan::Sampler(device, Vulkan::SamplerConfig()));

	// Transfer the data to device side.
	image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	image_->CopyFrom(commandPool, *stagingBuffer);

	// cannot done this on non-graphicbit queue
	//image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	// Delete the buffer before the memory
	stagingBuffer.reset();
}

TextureImage::~TextureImage()
{
	sampler_.reset();
	imageView_.reset();
	image_.reset();
	imageMemory_.reset();
}

void TextureImage::MainThreadPostLoading(Vulkan::CommandPool& commandPool)
{
	image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
}
