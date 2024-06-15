#include "RenderImage.hpp"
#include "Buffer.hpp"
#include "DepthBuffer.hpp"
#include "Device.hpp"
#include "Image.hpp"
#include "ImageMemoryBarrier.hpp"
#include "ImageView.hpp"
#include "SingleTimeCommands.hpp"
#include "Utilities/Exception.hpp"

namespace Vulkan {
    RenderImage::RenderImage(const Device& device,
        VkExtent2D extent,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        const char* debugName)
    {
        image_.reset(new Image(device, extent, format, tiling, usage));
        imageMemory_.reset(
            new DeviceMemory(image_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
        imageView_.reset(new ImageView(device, image_->Handle(), format, VK_IMAGE_ASPECT_COLOR_BIT));

        if(debugName)
        {
            const auto& debugUtils = device.DebugUtils();
            debugUtils.SetObjectName(image_->Handle(), debugName);
        }
    }

    RenderImage::~RenderImage()
    {
        image_.reset();
        imageMemory_.reset();
        imageView_.reset();
    }

    void RenderImage::InsertBarrier(VkCommandBuffer commandBuffer, VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask, VkImageLayout oldLayout, VkImageLayout newLayout) const
    {
        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        ImageMemoryBarrier::Insert(commandBuffer, GetImage().Handle(), subresourceRange,
                                  srcAccessMask, dstAccessMask, oldLayout,
                                  newLayout);
    }
}
