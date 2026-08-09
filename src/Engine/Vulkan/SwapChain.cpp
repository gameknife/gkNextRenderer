#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"
#include "Engine/Options.hpp"
#include "Engine/Utilities/Exception.hpp"
#include <algorithm>
#include <limits>


namespace Vulkan {

float SwapChain::uiContentScale_ = 1.0f;

namespace
{
    VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported)
    {
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> preference = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        for (const auto candidate : preference)
        {
            if ((supported & candidate) != 0)
            {
                return candidate;
            }
        }
        throw std::runtime_error("surface reports no supported composite alpha mode");
    }

    const char* FormatName(VkFormat format)
    {
        switch (format)
        {
        case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
        case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case VK_FORMAT_R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return "A2B10G10R10_UNORM_PACK32";
        case VK_FORMAT_A2R10G10B10_UNORM_PACK32: return "A2R10G10B10_UNORM_PACK32";
        default: return "unknown";
        }
    }

    const char* ColorSpaceName(VkColorSpaceKHR colorSpace)
    {
        switch (colorSpace)
        {
        case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR: return "SRGB_NONLINEAR";
        case VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT: return "DISPLAY_P3_NONLINEAR";
        case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT: return "EXTENDED_SRGB_LINEAR";
        case VK_COLOR_SPACE_EXTENDED_SRGB_NONLINEAR_EXT: return "EXTENDED_SRGB_NONLINEAR";
        case VK_COLOR_SPACE_HDR10_ST2084_EXT: return "HDR10_ST2084";
        case VK_COLOR_SPACE_HDR10_HLG_EXT: return "HDR10_HLG";
        case VK_COLOR_SPACE_DISPLAY_P3_LINEAR_EXT: return "DISPLAY_P3_LINEAR";
        default: return "unknown";
        }
    }

    bool IsExtendedSrgbLinearSurfaceFormat(const VkSurfaceFormatKHR& format)
    {
        return format.format == VK_FORMAT_R16G16B16A16_SFLOAT &&
               format.colorSpace == VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
    }

    bool IsHdr10SurfaceFormat(const VkSurfaceFormatKHR& format)
    {
        if (format.colorSpace != VK_COLOR_SPACE_HDR10_ST2084_EXT)
        {
            return false;
        }

        return format.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
               format.format == VK_FORMAT_A2R10G10B10_UNORM_PACK32;
    }
}

SwapChain::SwapChain(const class Device& device, const VkPresentModeKHR presentMode, bool forceSDR) :
    physicalDevice_(device.PhysicalDevice()),
    device_(device)
{
    const auto details = QuerySwapChainSupport(device.PhysicalDevice(), device.Surface().Handle());
    if (details.Formats.empty() || details.PresentModes.empty())
    {
        throw std::runtime_error("empty swap chain support");
    }

    const auto& surface = device.Surface();
    const auto& window = surface.Instance().Window();

    const auto surfaceFormat = ChooseSwapSurfaceFormat(details.Formats, forceSDR);
    const auto actualPresentMode = ChooseSwapPresentMode(details.PresentModes, presentMode);
    auto extent = ChooseSwapExtent(window, details.Capabilities);
    const auto imageCount = ChooseImageCount(details.Capabilities);

#if ANDROID
    float aspect = extent.width / static_cast<float>(extent.height);
    if( aspect < 1.0 )
    {
        uiContentScale_ = 1280.f / float(extent.height);
        
        extent.height = 1280;
        extent.width = floorf(1280 * aspect);
    }
    else
    {
        uiContentScale_ = 1280.f / float(extent.width);
        
        extent.height = 1280;
        extent.width = floorf(1280 / aspect);
    }
    SDL_SetWindowFullscreen(window.Handle(), true);
#endif
    
    VkSwapchainCreateInfoKHR createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface.Handle();
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    constexpr VkImageUsageFlags requiredUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if ((details.Capabilities.supportedUsageFlags & requiredUsage) != requiredUsage)
    {
        throw std::runtime_error(fmt::format(
            "surface usage 0x{:x} lacks required COLOR_ATTACHMENT/TRANSFER_DST flags 0x{:x}",
            details.Capabilities.supportedUsageFlags, requiredUsage));
    }
    VkImageUsageFlags optionalUsage =
        details.Capabilities.supportedUsageFlags &
        (VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_STORAGE_BIT);
    // Dozen advertises STORAGE on its surfaces but hands out DXGI back buffers, which cannot
    // be typed UAVs for BGRA8 in D3D12. Creating such a swapchain leaves its D3D12 device in
    // a state where every later allocation fails with VK_ERROR_OUT_OF_DEVICE_MEMORY, so drop
    // the flag here: the renderer already falls back to an intermediate target plus a blit.
    if (GOption != nullptr && GOption->VulkanDriver == "dozen")
    {
        optionalUsage &= ~VK_IMAGE_USAGE_STORAGE_BIT;
    }
    createInfo.imageUsage = requiredUsage | optionalUsage;
    createInfo.preTransform = details.Capabilities.currentTransform;
    createInfo.compositeAlpha = ChooseCompositeAlpha(details.Capabilities.supportedCompositeAlpha);
    createInfo.presentMode = actualPresentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = nullptr;
    
    if (device.GraphicsFamilyIndex() != device.PresentFamilyIndex())
    {
        uint32_t queueFamilyIndices[] = { device.GraphicsFamilyIndex(), device.PresentFamilyIndex() };

        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    }
    else
    {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        createInfo.queueFamilyIndexCount = 0; // Optional
        createInfo.pQueueFamilyIndices = nullptr; // Optional
    }

    Check(Interposer().CreateSwapchainKHR(device.Handle(), &createInfo, nullptr, &swapChain_),
        "create swap chain!");

    minImageCount_ = std::max(2u, details.Capabilities.minImageCount);
    presentMode_ = actualPresentMode;
    format_ = surfaceFormat.format;
    colorSpace_ = surfaceFormat.colorSpace;
    imageUsage_ = createInfo.imageUsage;
    extent_ = extent;
    renderExtent_ = extent_;
    renderOffset_ = {0,0};

    SPDLOG_INFO("Swap Chain format: {} ({}) colorSpace: {} ({}) outputMode: {} usage: 0x{:x} compositeAlpha: 0x{:x}",
                FormatName(format_), static_cast<int>(format_),
                ColorSpaceName(colorSpace_), static_cast<int>(colorSpace_),
                static_cast<int>(outputMode_), imageUsage_,
                static_cast<uint32_t>(createInfo.compositeAlpha));
    if (!SupportsUsage(VK_IMAGE_USAGE_STORAGE_BIT))
    {
        SPDLOG_WARN("Swapchain STORAGE usage is unavailable; using intermediate render target + blit fallback");
    }
    if (!SupportsUsage(VK_IMAGE_USAGE_TRANSFER_SRC_BIT))
    {
        SPDLOG_WARN("Swapchain TRANSFER_SRC usage is unavailable; swapchain readback features are disabled");
    }
    
    images_ = GetEnumerateVector(device_.Handle(), swapChain_,
        +[](VkDevice device, VkSwapchainKHR swapchain, uint32_t* count, VkImage* images)
        { return Interposer().GetSwapchainImagesKHR(device, swapchain, count, images); });
    imageViews_.reserve(images_.size());

    for (const auto image : images_)
    {
        imageViews_.push_back(std::make_unique<ImageView>(device, image, format_, VK_IMAGE_ASPECT_COLOR_BIT));
    }

    const auto& debugUtils = device.DebugUtils();

    for (size_t i = 0; i != images_.size(); ++i)
    {
        debugUtils.SetObjectName(images_[i], ("Swapchain Image #" + std::to_string(i)).c_str());
        debugUtils.SetObjectName(imageViews_[i]->Handle(), ("Swapchain ImageView #" + std::to_string(i)).c_str());
    }
}

SwapChain::~SwapChain()
{
    imageViews_.clear();

    if (swapChain_ != nullptr)
    {
        Interposer().DestroySwapchainKHR(device_.Handle(), swapChain_, nullptr);
        swapChain_ = nullptr;
    }
}

void SwapChain::UpdateRenderViewport(int32_t x, int32_t y, uint32_t width, uint32_t height) const
{
    renderExtent_ = { width, height };
    renderOffset_ = { x, y };
}

void SwapChain::UpdateOutputViewport(int32_t x, int32_t y, uint32_t width, uint32_t height) const
{
    outputExtent_ = { width, height };
    outputOffset_ = { x, y };
}

SwapChain::SupportDetails SwapChain::QuerySwapChainSupport(VkPhysicalDevice physicalDevice, const VkSurfaceKHR surface)
{
    SupportDetails details;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.Capabilities);
    details.Formats = GetEnumerateVector(physicalDevice, surface, vkGetPhysicalDeviceSurfaceFormatsKHR);
    details.PresentModes = GetEnumerateVector(physicalDevice, surface, vkGetPhysicalDeviceSurfacePresentModesKHR);

    return details;
}

VkSurfaceFormatKHR SwapChain::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats, bool forceSDR) 
{
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) 
    {
        return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    }
    outputMode_ = ESwapChainOutputMode::SDR;
    
    // hdr first
    if(!forceSDR)
    {
#if _APPLE_
        for (const auto& format : formats)
        {

            if (IsExtendedSrgbLinearSurfaceFormat(format))
            {
                outputMode_ = ESwapChainOutputMode::ExtendedSrgbLinear;
                return format;
            }
        }
#endif

        for (const auto& format : formats)
        {
            if (IsHdr10SurfaceFormat(format))
            {
                outputMode_ = ESwapChainOutputMode::HDR10_ST2084;
                return format;
            }
        }
    }

#if ANDROID
    // display p3 for android
    for (const auto& format : formats)
    {
        
        if (format.colorSpace == VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT)
        {
            return format;
        }
    }
#endif
    
    // sdr fallback
    for (const auto& format : formats)
    {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    for (const auto& format : formats)
    {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            return format;
        }
    }

    // bad driver
    Throw(std::runtime_error("found no suitable surface format"));
}

VkPresentModeKHR SwapChain::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& presentModes, const VkPresentModeKHR presentMode)
{
    // VK_PRESENT_MODE_IMMEDIATE_KHR specifies that the presentation engine does not wait for a vertical blanking period
    // to update the current image, meaning this mode may result in visible tearing. No internal queuing of presentation
    // requests is needed, as the requests are applied immediately.

    // VK_PRESENT_MODE_MAILBOX_KHR specifies that the presentation engine waits for the next vertical blanking period to
    // update the current image. Tearing cannot be observed. An internal single-entry queue is used to hold pending
    // presentation requests. If the queue is full when a new presentation request is received, the new request replaces
    // the existing entry, and any images associated with the prior entry become available for re-use by the application.
    // One request is removed from the queue and processed during each vertical blanking period in which the queue is non-empty.

    // VK_PRESENT_MODE_FIFO_KHR specifies that the presentation engine waits for the next vertical blanking period to update
    // the current image. Tearing cannot be observed. An internal queue is used to hold pending presentation requests.
    // New requests are appended to the end of the queue, and one request is removed from the beginning of the queue and
    // processed during each vertical blanking period in which the queue is non-empty. This is the only value of presentMode
    // that is required to be supported.

    // VK_PRESENT_MODE_FIFO_RELAXED_KHR specifies that the presentation engine generally waits for the next vertical blanking
    // period to update the current image. If a vertical blanking period has already passed since the last update of the current
    // image then the presentation engine does not wait for another vertical blanking period for the update, meaning this mode
    // may result in visible tearing in this case. This mode is useful for reducing visual stutter with an application that will
    // mostly present a new image before the next vertical blanking period, but may occasionally be late, and present a new
    // image just after the next vertical blanking period. An internal queue is used to hold pending presentation requests.
    // New requests are appended to the end of the queue, and one request is removed from the beginning of the queue and
    // processed during or after each vertical blanking period in which the queue is non-empty.


    switch (presentMode)
    {
    case VK_PRESENT_MODE_IMMEDIATE_KHR:
    case VK_PRESENT_MODE_MAILBOX_KHR:
    case VK_PRESENT_MODE_FIFO_KHR:
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
        
        if (std::find(presentModes.begin(), presentModes.end(), presentMode) != presentModes.end())
        {
            return presentMode;
        }

        break;

    default:
        Throw(std::out_of_range("unknown present mode"));
    }

    // Fallback
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D SwapChain::ChooseSwapExtent(const Window& window, const VkSurfaceCapabilitiesKHR& capabilities)
{
    // Vulkan tells us to match the resolution of the window by setting the width and height in the currentExtent member.
    // However, some window managers do allow us to differ here and this is indicated by setting the width and height in
    // currentExtent to a special value: the maximum value of uint32_t. In that case we'll pick the resolution that best 
    // matches the window within the minImageExtent and maxImageExtent bounds.
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    auto actualExtent = window.FramebufferSize();

    actualExtent.width = std::max(capabilities.minImageExtent.width, std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(capabilities.minImageExtent.height, std::min(capabilities.maxImageExtent.height, actualExtent.height));

    return actualExtent;
}

uint32_t SwapChain::ChooseImageCount(const VkSurfaceCapabilitiesKHR& capabilities)
{
    // The implementation specifies the minimum amount of images to function properly
    // and we'll try to have one more than that to properly implement triple buffering.
    // (tanguyf: or not, we can just rely on VK_PRESENT_MODE_MAILBOX_KHR with two buffers)
    uint32_t imageCount = std::max(2u, capabilities.minImageCount);// +1; 
    
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }

    return imageCount;
}

}
