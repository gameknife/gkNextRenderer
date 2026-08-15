#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/Device.hpp"
#include <cstring>


namespace Assets {

TextureImage::TextureImage(Vulkan::CommandPool& commandPool,
                           size_t width,
                           size_t height,
                           uint32_t miplevel,
                           VkFormat format,
                           const unsigned char* data,
                           uint32_t size,
                           VkComponentMapping componentMapping)
{
    // Create a host staging buffer and copy the image into it.
    const VkDeviceSize imageSize = size;
    const auto& device = commandPool.Device();

    // Create the device side image, memory, view and sampler.
    image_.reset(new Vulkan::Image(device, VkExtent2D{ static_cast<uint32_t>(width), static_cast<uint32_t>(height) }, miplevel, format));
    imageMemory_.reset(new Vulkan::DeviceMemory(image_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
    imageView_.reset(new Vulkan::ImageView(
        device, image_->Handle(), image_->Format(), VK_IMAGE_ASPECT_COLOR_BIT, miplevel, componentMapping));
    device.DebugUtils().SetObjectName(image_->Handle(), "TextureImage Image");
    imageMemory_->SetName("TextureImage Memory");
    device.DebugUtils().SetObjectName(imageView_->Handle(), "TextureImage ImageView");
    
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
        device.DebugUtils().SetObjectName(stagingBuffer->Handle(), "TextureImage Upload Staging Buffer");
        stagingBufferMemory.SetName("TextureImage Upload Staging Memory");

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
        image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
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
    device.DebugUtils().SetObjectName(image_->Handle(), "TextureImage Mipmapped Image");
    imageMemory_->SetName("TextureImage Mipmapped Memory");
    device.DebugUtils().SetObjectName(imageView_->Handle(), "TextureImage Mipmapped ImageView");
    
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
        device.DebugUtils().SetObjectName(stagingBuffer->Handle(), "TextureImage Mip Upload Staging Buffer");
        stagingBufferMemory.SetName("TextureImage Mip Upload Staging Memory");
        
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

    // Create a temporary staging buffer and copy the data into it.
    auto stagingBuffer = std::make_unique<Vulkan::Buffer>(device, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    auto stagingBufferMemory = stagingBuffer->AllocateMemory(
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    device.DebugUtils().SetObjectName(stagingBuffer->Handle(), "TextureImage Update Staging Buffer");
    stagingBufferMemory.SetName("TextureImage Update Staging Memory");

    // Map the memory and copy the data.
    const auto stagingData = stagingBufferMemory.Map(0, size);
    std::memcpy(stagingData, data, size);
    stagingBufferMemory.Unmap();

    // Transition the image from shader-read to transfer-destination layout.
    image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Define the copy region.
    VkBufferImageCopy region{};
    region.bufferOffset = (sourceWidth * startY + startX) * 4;  // 4 bytes per pixel
    region.bufferRowLength = sourceWidth;  // Tightly packed.
    region.bufferImageHeight = sourceHeight;  // Tightly packed.

    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;

    region.imageOffset = {static_cast<int32_t>(startX), static_cast<int32_t>(startY), 0};
    region.imageExtent = {width, height, 1};

    // Execute the region copy.
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

    // This API updates sampled textures. Keep its declared image layout in sync
    // with the bindless sampled descriptor after every upload.
    image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Release temporary resources.
    stagingBuffer.reset();
}

void TextureImage::SetDebugName(const std::string& debugName)
{
    const auto& debugUtils = image_->Device().DebugUtils();
    debugUtils.SetObjectName(image_->Handle(), debugName.c_str());
    if (imageMemory_)
    {
        imageMemory_->SetName((debugName + " Memory").c_str());
    }
    if (imageView_)
    {
        debugUtils.SetObjectName(imageView_->Handle(), (debugName + " ImageView").c_str());
    }
}

void TextureImage::MainThreadPostLoading(Vulkan::CommandPool& commandPool)
{
    image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
}
