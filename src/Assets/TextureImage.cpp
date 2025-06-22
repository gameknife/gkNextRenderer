#include "TextureImage.hpp"
#include "Texture.hpp"
#include "Vulkan/Buffer.hpp"
#include "Vulkan/CommandPool.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/Sampler.hpp"
#include <cstring>

#include "Utilities/Console.hpp"
#include "Vulkan/SingleTimeCommands.hpp"

namespace Assets {

TextureImage::TextureImage(Vulkan::CommandPool& commandPool, size_t width, size_t height, uint32_t miplevel, VkFormat format, const unsigned char* data, uint32_t size)
{
	// Create a host staging buffer and copy the image into it.
	const VkDeviceSize imageSize = size;
	const auto& device = commandPool.Device();

	// Create the device side image, memory, view and sampler.
	image_.reset(new Vulkan::Image(device, VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) }, miplevel, format));
	imageMemory_.reset(new Vulkan::DeviceMemory(image_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	imageView_.reset(new Vulkan::ImageView(device, image_->Handle(), image_->Format(), VK_IMAGE_ASPECT_COLOR_BIT));
	
	Vulkan::SamplerConfig samplerConfig;
	if (format == VK_FORMAT_R32_UINT || format == VK_FORMAT_R32_SINT)
	{
		samplerConfig.MagFilter = VK_FILTER_NEAREST;
		samplerConfig.MinFilter = VK_FILTER_NEAREST;
		samplerConfig.MipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	}
	sampler_.reset(new Vulkan::Sampler(device, samplerConfig));

	if(data)
	{
		auto stagingBuffer = std::make_unique<Vulkan::Buffer>(device, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
		auto stagingBufferMemory = stagingBuffer->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

		const auto stagingData = stagingBufferMemory.Map(0, imageSize);
		std::memcpy(stagingData, data, imageSize);
		stagingBufferMemory.Unmap();


		// Transfer the data to device side.
		image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
		image_->CopyFrom(commandPool, *stagingBuffer);

		// Delete the buffer before the memory
		stagingBuffer.reset();
	}
	else
	{
		//image_->TransitionImageLayout( commandPool, VK_IMAGE_LAYOUT_GENERAL);
	}

	// cannot done this on non-graphicbit queue
	//image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

TextureImage::TextureImage(
    Vulkan::CommandPool& commandPool, 
    size_t width, 
    size_t height, 
    uint32_t mipLevels, 
    VkFormat format, 
    const unsigned char* baseData, 
    uint32_t baseSize,
    const std::vector<std::vector<float>>& mipLevelData, 
    const std::vector<std::pair<int, int>>& mipDimensions)
{
    const auto& device = commandPool.Device();
    
    // Create the device side image, memory, view and sampler
    image_.reset(new Vulkan::Image(device, VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) }, mipLevels, format));
    imageMemory_.reset(new Vulkan::DeviceMemory(image_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
    imageView_.reset(new Vulkan::ImageView(device, image_->Handle(), image_->Format(), VK_IMAGE_ASPECT_COLOR_BIT, mipLevels));
    
    // Configure sampler for mipmap levels
    Vulkan::SamplerConfig samplerConfig;
    samplerConfig.MipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerConfig.MinLod = 0.0f;
    samplerConfig.MaxLod = static_cast<float>(mipLevels);
    samplerConfig.MipLodBias = 0.0f;
    sampler_.reset(new Vulkan::Sampler(device, samplerConfig));

    // Transition image layout for transfer
    image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Upload each mip level
    for (uint32_t mipLevel = 0; mipLevel < mipLevels; ++mipLevel) {
        VkDeviceSize mipSize;
        const void* mipData;
        uint32_t mipWidth, mipHeight;
        
        if (mipLevel == 0) {
            // Base level
            mipSize = baseSize;
            mipData = baseData;
            mipWidth = static_cast<uint32_t>(width);
            mipHeight = static_cast<uint32_t>(height);
        } else {
            // Pre-calculated mip levels
            mipWidth = static_cast<uint32_t>(mipDimensions[mipLevel].first);
            mipHeight = static_cast<uint32_t>(mipDimensions[mipLevel].second);
            mipSize = mipWidth * mipHeight * 4 * sizeof(float);
            mipData = mipLevelData[mipLevel].data();
        }
        
        // Create staging buffer for this mip level
        auto stagingBuffer = std::make_unique<Vulkan::Buffer>(device, mipSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        auto stagingBufferMemory = stagingBuffer->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        
        // Copy data to staging buffer
        const auto stagingData = stagingBufferMemory.Map(0, mipSize);
        std::memcpy(stagingData, mipData, mipSize);
        stagingBufferMemory.Unmap();
        
        // Copy from staging buffer to specific mip level
        image_->CopyFromToMipLevel(commandPool, *stagingBuffer, mipLevel, mipWidth, mipHeight);
        
        // Clean up staging resources for this mip level
        stagingBuffer.reset();
    }
    
    // Cannot transition to shader read only on non-graphics queue
    // Will be done in MainThreadPostLoading
}
	
TextureImage::~TextureImage()
{
	sampler_.reset();
	imageView_.reset();
	image_.reset();
	imageMemory_.reset();
}

void TextureImage::UpdateDataMainThread(
    Vulkan::CommandPool& commandPool,
    uint32_t startX,
    uint32_t startY,
    uint32_t width,
    uint32_t height,
    uint32_t sourceWidth,
    uint32_t sourceHeight,
    const unsigned char* data,
    uint32_t size)
{
    const auto& device = commandPool.Device();

    // 创建临时暂存缓冲区并复制数据
    auto stagingBuffer = std::make_unique<Vulkan::Buffer>(device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    auto stagingBufferMemory = stagingBuffer->AllocateMemory(
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 映射内存并复制数据
    const auto stagingData = stagingBufferMemory.Map(0, size);
    std::memcpy(stagingData, data, size);
    stagingBufferMemory.Unmap();

    // 将图像从着色器读取转换为传输目标布局
    image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // 定义复制区域
    VkBufferImageCopy region{};
    region.bufferOffset = (sourceWidth * startY + startX) * 4;  // 4 bytes per pixel
    region.bufferRowLength = sourceWidth;  // 紧凑排列
    region.bufferImageHeight = sourceHeight;  // 紧凑排列

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {static_cast<int32_t>(startX), static_cast<int32_t>(startY), 0};
    region.imageExtent = {width, height, 1};

    // 执行区域复制
    Vulkan::SingleTimeCommands::Submit(commandPool, [&](VkCommandBuffer commandBuffer)
    {
        vkCmdCopyBufferToImage(
            commandBuffer,
            stagingBuffer->Handle(),
            image_->Handle(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region);
    });

    // 复制完成后，将图像转换回着色器只读布局
    image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_GENERAL);

    // 清理临时资源
    stagingBuffer.reset();
}

void TextureImage::SetDebugName(const std::string& debugName)
{
	const auto& debugUtils = image_->Device().DebugUtils();
	debugUtils.SetObjectName(image_->Handle(), debugName.c_str());
}

void TextureImage::MainThreadPostLoading(Vulkan::CommandPool& commandPool)
{
	image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
}
