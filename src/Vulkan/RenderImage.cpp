#include "RenderImage.hpp"
#include "Buffer.hpp"
#include "DepthBuffer.hpp"
#include "Device.hpp"
#include "Image.hpp"
#include "ImageMemoryBarrier.hpp"
#include "ImageView.hpp"
#include "SingleTimeCommands.hpp"
#include "Utilities/Exception.hpp"
#include "Vulkan/RayTracing/DeviceProcedures.hpp"

namespace Vulkan {
    RenderImage::RenderImage(const Device& device,
        VkExtent2D extent,
        VkFormat format,
        VkImageTiling tiling,
        VkImageUsageFlags usage,
        bool external,
        const char* debugName)
    {
        image_.reset(new Image(device, extent, format, tiling, usage, external));
        imageMemory_.reset(
            new DeviceMemory(image_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, external)));
        imageView_.reset(new ImageView(device, image_->Handle(), format, VK_IMAGE_ASPECT_COLOR_BIT));

        if(debugName)
        {
            const auto& debugUtils = device.DebugUtils();
            debugUtils.SetObjectName(image_->Handle(), debugName);
        }

        sampler_.reset(new Vulkan::Sampler(device, Vulkan::SamplerConfig()));
    }

    RenderImage::~RenderImage()
    {
        sampler_.reset();
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

    ExtHandle RenderImage::GetExternalHandle() const
    {
        ExtHandle handle{};
        #if WIN32 && !defined(__MINGW32__)
        VkMemoryGetWin32HandleInfoKHR handleInfo = { VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
        handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        handleInfo.memory = imageMemory_->Handle();
        image_->Device().GetDeviceProcedures().vkGetMemoryWin32HandleKHR(image_->Device().Handle(), &handleInfo, &handle);
        #endif
        return handle;
    }
}
