#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

#include <array>

namespace Vulkan
{
namespace
{
    void SetupQueueSharing(
        const Device& device,
        VkSharingMode& sharingMode,
        uint32_t& queueFamilyIndexCount,
        const uint32_t*& queueFamilyIndices,
        std::array<uint32_t, 2>& queueFamilyIndexStorage)
    {
        if (device.TransferFamilyIndex() != static_cast<int32_t>(device.GraphicsFamilyIndex()))
        {
            queueFamilyIndexStorage =
            {
                device.GraphicsFamilyIndex(),
                static_cast<uint32_t>(device.TransferFamilyIndex())
            };
            sharingMode = VK_SHARING_MODE_CONCURRENT;
            queueFamilyIndexCount = static_cast<uint32_t>(queueFamilyIndexStorage.size());
            queueFamilyIndices = queueFamilyIndexStorage.data();
            return;
        }

        sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        queueFamilyIndexCount = 0;
        queueFamilyIndices = nullptr;
    }
}

// ============================================================================
// Image
// ============================================================================

Image::Image(const class Device& device, const VkExtent2D extent, uint32_t miplevel, const VkFormat format) :
    Image(device, extent, miplevel, format,  VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false)
{
}

Image::Image(
    const class Device& device,
    const VkExtent2D extent,
    const uint32_t miplevel,
    const VkFormat format,
    const VkImageTiling tiling,
    const VkImageUsageFlags usage,
    bool useForExternal) :
    device_(device),
    extent_(extent),
    format_(format),
    imageLayout_(VK_IMAGE_LAYOUT_UNDEFINED),
    mipLevel_(miplevel),
    external_(useForExternal)
{
    VkImageCreateInfo imageInfo = {};
    std::array<uint32_t, 2> queueFamilyIndexStorage{};
    const uint32_t* queueFamilyIndices = nullptr;
    uint32_t queueFamilyIndexCount = 0;
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = extent.width;
    imageInfo.extent.height = extent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = miplevel;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = imageLayout_;
    imageInfo.usage = usage;
    SetupQueueSharing(device_, imageInfo.sharingMode, queueFamilyIndexCount, queueFamilyIndices, queueFamilyIndexStorage);
    imageInfo.queueFamilyIndexCount = queueFamilyIndexCount;
    imageInfo.pQueueFamilyIndices = queueFamilyIndices;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0; // Optional

    VkExternalMemoryImageCreateInfo externalMemoryImageInfo{};
    externalMemoryImageInfo.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
#if WIN32
    externalMemoryImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else
    externalMemoryImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif

#if !ANDROID
    if(external_)
    {
        imageInfo.pNext = &externalMemoryImageInfo;
    }
#endif

    Check(vkCreateImage(device.Handle(), &imageInfo, nullptr, &image_),
        "create image");
}

Image::Image(Image&& other) noexcept :
    device_(other.device_),
    extent_(other.extent_),
    format_(other.format_),
    imageLayout_(other.imageLayout_),
    image_(other.image_)
{
    other.image_ = nullptr;
}

Image::~Image()
{
    if (image_ != nullptr)
    {
        vkDestroyImage(device_.Handle(), image_, nullptr);
        image_ = nullptr;
    }
}

DeviceMemory Image::AllocateMemory(const VkMemoryPropertyFlags properties, bool external, bool dedicated) const
{
    return DeviceMemory(device_, *this, properties, external, dedicated);
}

VkMemoryRequirements Image::GetMemoryRequirements() const
{
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device_.Handle(), image_, &requirements);
    return requirements;
}

void Image::TransitionImageLayout(CommandPool& commandPool, VkImageLayout newLayout)
{
    SingleTimeCommands::Submit(commandPool, [&](VkCommandBuffer commandBuffer)
    {
        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = imageLayout_;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image_;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mipLevel_;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
            newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

            if (DepthBuffer::HasStencilComponent(format_))
            {
                barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }
        }
        else
        {
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        }

        VkPipelineStageFlags sourceStage;
        VkPipelineStageFlags destinationStage;

        if (imageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (imageLayout_ == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (imageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        }
        else if (imageLayout_ == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        }
        else if (newLayout == VK_IMAGE_LAYOUT_GENERAL)
        {
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }
        else
        {
            Throw(std::invalid_argument("unsupported layout transition"));
        }

        vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    });

    imageLayout_ = newLayout;
}

void Image::CopyFrom(CommandPool& commandPool, const Buffer& buffer)
{
    SingleTimeCommands::Submit(commandPool, [&](VkCommandBuffer commandBuffer)
    {
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = { extent_.width, extent_.height, 1 };

        vkCmdCopyBufferToImage(commandBuffer, buffer.Handle(), image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    });
}

void Image::CopyFromToMipLevel(
    CommandPool& commandPool,
    const Buffer& buffer,
    uint32_t mipLevel,
    uint32_t mipWidth,
    uint32_t mipHeight)
{
    SingleTimeCommands::Submit(commandPool, [&](VkCommandBuffer commandBuffer)
    {
        VkBufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;

        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = mipLevel;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        region.imageOffset = {0, 0, 0};
        region.imageExtent = {mipWidth, mipHeight, 1};

        vkCmdCopyBufferToImage(
            commandBuffer,
            buffer.Handle(),
            image_,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region);
    });
}

// ============================================================================
// ImageView
// ============================================================================

ImageView::ImageView(const class Device& device,
                     const VkImage image,
                     const VkFormat format,
                     const VkImageAspectFlags aspectFlags,
                     const uint32_t miplevel,
                     const VkComponentMapping components) :
    device_(device)
{
    VkImageViewCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = format;
    createInfo.components = components;
    createInfo.subresourceRange.aspectMask = aspectFlags;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = miplevel;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    Check(vkCreateImageView(device_.Handle(), &createInfo, nullptr, &imageView_),
        "create image view");
}

ImageView::~ImageView()
{
    if (imageView_ != nullptr)
    {
        vkDestroyImageView(device_.Handle(), imageView_, nullptr);
        imageView_ = nullptr;
    }
}

// ============================================================================
// Buffer
// ============================================================================

Buffer::Buffer(const class Device& device, const size_t size, const VkBufferUsageFlags usage) :
    device_(device)
{
    VkBufferCreateInfo bufferInfo = {};
    std::array<uint32_t, 2> queueFamilyIndexStorage{};
    const uint32_t* queueFamilyIndices = nullptr;
    uint32_t queueFamilyIndexCount = 0;
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    SetupQueueSharing(device_, bufferInfo.sharingMode, queueFamilyIndexCount, queueFamilyIndices, queueFamilyIndexStorage);
    bufferInfo.queueFamilyIndexCount = queueFamilyIndexCount;
    bufferInfo.pQueueFamilyIndices = queueFamilyIndices;

    Check(vkCreateBuffer(device.Handle(), &bufferInfo, nullptr, &buffer_),
        "create buffer");
}

Buffer::~Buffer()
{
    if (buffer_ != nullptr)
    {
        vkDestroyBuffer(device_.Handle(), buffer_, nullptr);
        buffer_ = nullptr;
    }
}

DeviceMemory Buffer::AllocateMemory(
    const VkMemoryPropertyFlags propertyFlags)
{
    return DeviceMemory(device_, *this, propertyFlags);
}

DeviceMemory Buffer::AllocateMemory(
    const VkMemoryPropertyFlags propertyFlags,
    const DeviceMemory::BufferAllocationOptions& options)
{
    return DeviceMemory(device_, *this, propertyFlags, options);
}

VkMemoryRequirements Buffer::GetMemoryRequirements() const
{
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device_.Handle(), buffer_, &requirements);
    return requirements;
}

VkDeviceAddress Buffer::GetDeviceAddress() const
{
    VkBufferDeviceAddressInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.pNext = nullptr;
    info.buffer = Handle();
    return vkGetBufferDeviceAddress(device_.Handle(), &info);
}

void Buffer::CopyFrom(CommandPool& commandPool, const Buffer& src, VkDeviceSize size)
{
    SingleTimeCommands::Submit(commandPool, [&](VkCommandBuffer commandBuffer)
    {
        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset = 0; // Optional
        copyRegion.dstOffset = 0; // Optional
        copyRegion.size = size;

        vkCmdCopyBuffer(commandBuffer, src.Handle(), Handle(), 1, &copyRegion);
    });
}

void Buffer::CopyTo(CommandPool& commandPool, const Buffer& dst, VkDeviceSize size)
{
    SingleTimeCommands::Submit(commandPool, [&](VkCommandBuffer commandBuffer)
    {
        VkBufferCopy copyRegion = {};
        copyRegion.srcOffset = 0; // Optional
        copyRegion.dstOffset = 0; // Optional
        copyRegion.size = size;

        vkCmdCopyBuffer(commandBuffer, Handle(), dst.Handle(), 1, &copyRegion);
    });
}

// ============================================================================
// DepthBuffer
// ============================================================================

namespace
{
    VkFormat FindSupportedFormat(const Device& device, const std::vector<VkFormat>& candidates, const VkImageTiling tiling, const VkFormatFeatureFlags features)
    {
        for (auto format : candidates)
        {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(device.PhysicalDevice(), format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features)
            {
                return format;
            }

            if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features)
            {
                return format;
            }
        }

        Throw(std::runtime_error("failed to find supported format"));
    }

    VkFormat FindDepthFormat(const Device& device)
    {
        return FindSupportedFormat(
            device,
            { VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }
}

DepthBuffer::DepthBuffer(CommandPool& commandPool, const VkExtent2D extent) :
    format_(FindDepthFormat(commandPool.Device()))
{
    const auto& device = commandPool.Device();

    image_.reset(new Image(device, extent, 1, format_, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT));
    imageMemory_.reset(new DeviceMemory(image_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, true)));
    imageView_.reset(new class ImageView(device, image_->Handle(), format_, VK_IMAGE_ASPECT_DEPTH_BIT));

    image_->TransitionImageLayout(commandPool, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

    const auto& debugUtils = device.DebugUtils();

    debugUtils.SetObjectName(image_->Handle(), "Depth Buffer Image");
    imageMemory_->SetName("Depth Buffer Image Memory");
    debugUtils.SetObjectName(imageView_->Handle(), "Depth Buffer ImageView");
}

DepthBuffer::~DepthBuffer()
{
    imageView_.reset();
    image_.reset();
    imageMemory_.reset(); // release memory after bound image has been destroyed
}

// ============================================================================
// RenderImage
// ============================================================================

RenderImage::RenderImage(const Device& device,
    VkExtent2D extent,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    bool external,
    const char* debugName)
{
    image_.reset(new Image(device, extent, 1, format, tiling, usage, external));
    imageMemory_.reset(
        new DeviceMemory(image_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, external, true)));
    imageView_.reset(new ImageView(device, image_->Handle(), format, VK_IMAGE_ASPECT_COLOR_BIT));

    if(debugName)
    {
        const auto& debugUtils = device.DebugUtils();
        debugUtils.SetObjectName(image_->Handle(), debugName);
        imageMemory_->SetName((std::string(debugName) + " Memory").c_str());
        debugUtils.SetObjectName(imageView_->Handle(), (std::string(debugName) + " View").c_str());
    }

    sampler_.reset(new Vulkan::Sampler(device, Vulkan::SamplerConfig()));
}

RenderImage::~RenderImage()
{
    sampler_.reset();
    imageView_.reset();
    image_.reset();
    imageMemory_.reset();
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
#if WIN32
    VkMemoryGetWin32HandleInfoKHR handleInfo = { VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
    handleInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    handleInfo.memory = imageMemory_->Handle();
    image_->Device().GetDeviceProcedures().vkGetMemoryWin32HandleKHR(image_->Device().Handle(), &handleInfo, &handle);
#else
    VkMemoryGetFdInfoKHR fdInfo = { VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
    fdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    fdInfo.memory = imageMemory_->Handle();
    image_->Device().GetDeviceProcedures().vkGetMemoryFdKHR(image_->Device().Handle(), &fdInfo, &handle);
#endif
    return handle;
}

}
