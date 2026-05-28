#include "Engine/Runtime/Editor/UserInterface.hpp"

#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Scene/SceneList.hpp"
#include "Engine/Runtime/Config/UserSettings.hpp"
#include "Engine/Runtime/Config/CVarSystem.hpp"
#include "Engine/Runtime/Editor/ConsoleLogBuffer.hpp"
#include "Engine/Runtime/Editor/FontLoader.h"
#include "Engine/Runtime/Editor/ProfessionalUI.hpp"
#include "ThirdParty/imgui-custom/imgui_impl_sdl3_custom.h"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_stdlib.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "ThirdParty/fontawesome/IconsFontAwesome6.h"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/ImGui.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Utilities/StbImage.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

extern float GAndroidMagicScale;
extern std::unique_ptr<Vulkan::VulkanBaseRenderer> GApplication;

namespace NextUI
{

namespace
{
    std::string GetPhysicalDeviceDriverName(VkPhysicalDevice physicalDevice)
    {
        if (physicalDevice == VK_NULL_HANDLE)
        {
            return {};
        }

        VkPhysicalDeviceDriverProperties driverProperties{};
        driverProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

        VkPhysicalDeviceProperties2 deviceProperties{};
        deviceProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        deviceProperties.pNext = &driverProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProperties);

        return driverProperties.driverName[0] != '\0' ? std::string(driverProperties.driverName) : std::string{};
    }

    constexpr const char* kUiVertexShaderPath = "assets/shaders/UI.ImGui.vert.slang.spv";
    constexpr const char* kUiFragmentShaderPath = "assets/shaders/UI.ImGui.frag.slang.spv";
    constexpr const char* kUiFontAtlasTextureName = "__imgui_font_atlas__";
    constexpr float kUiHdrReferenceWhiteNit = 203.0f;

    struct UiPushConstants
    {
        float scale[2];
        float translate[2];
        float rotation[4];
        uint32_t hdrOutput;
        float hdrReferenceWhiteNit;
        float padding[2];
    };

    struct UiBatchedVertex
    {
        ImVec2 position;
        ImVec2 uv;
        ImU32 color = 0;
        float clipRect[4]{};
        uint32_t textureIndex = 0;
    };

    struct UiDrawSegment
    {
        uint32_t vertexOffset = 0;
        uint32_t vertexCount = 0;
    };

    struct UiDrawOp
    {
        enum class EType : uint8_t
        {
            Draw,
            Callback,
        };

        EType type = EType::Draw;
        UiDrawSegment segment{};
        const ImDrawList* drawList = nullptr;
        const ImDrawCmd* drawCmd = nullptr;
    };

    struct UiRendererRenderState
    {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    };

    struct UiPlatformFrame
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkImage backbuffer = VK_NULL_HANDLE;
        VkImageView backbufferView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
    };

    struct UiPlatformFrameSemaphores
    {
        VkSemaphore imageAcquiredSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderCompleteSemaphore = VK_NULL_HANDLE;
    };

    struct UiPlatformWindow
    {
        int width = 0;
        int height = 0;
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkSurfaceFormatKHR surfaceFormat{};
        VkPresentModeKHR presentMode = static_cast<VkPresentModeKHR>(~0);
        VkRenderPass renderPass = VK_NULL_HANDLE;
        bool useDynamicRendering = false;
        bool clearEnable = true;
        VkClearValue clearValue{};
        uint32_t frameIndex = 0;
        uint32_t imageCount = 0;
        uint32_t semaphoreCount = 0;
        uint32_t semaphoreIndex = 0;
        ImVector<UiPlatformFrame> frames;
        ImVector<UiPlatformFrameSemaphores> frameSemaphores;
    };

    struct UiPlatformViewportData
    {
        UiPlatformWindow window;
        bool windowOwned = false;
        bool swapChainNeedRebuild = false;
        bool swapChainSuboptimal = false;
    };

    VkSurfaceFormatKHR SelectPlatformSurfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                                   const VkFormat* requestFormats, int requestFormatsCount,
                                                   VkColorSpaceKHR requestColorSpace)
    {
        uint32_t availableCount = 0;
        Vulkan::Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &availableCount, nullptr),
                      "query ui platform surface format count");

        ImVector<VkSurfaceFormatKHR> availableFormats;
        availableFormats.resize(static_cast<int>(availableCount));
        Vulkan::Check(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &availableCount,
                                                           availableFormats.Data),
                      "query ui platform surface formats");

        if (availableCount == 1)
        {
            if (availableFormats[0].format == VK_FORMAT_UNDEFINED)
            {
                VkSurfaceFormatKHR format{};
                format.format = requestFormats[0];
                format.colorSpace = requestColorSpace;
                return format;
            }
            return availableFormats[0];
        }

        for (int requestedIndex = 0; requestedIndex < requestFormatsCount; ++requestedIndex)
        {
            for (uint32_t availableIndex = 0; availableIndex < availableCount; ++availableIndex)
            {
                if (availableFormats[availableIndex].format == requestFormats[requestedIndex] &&
                    availableFormats[availableIndex].colorSpace == requestColorSpace)
                {
                    return availableFormats[availableIndex];
                }
            }
        }

        return availableFormats[0];
    }

    VkPresentModeKHR SelectPlatformPresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                               const VkPresentModeKHR* requestModes, int requestModesCount)
    {
        uint32_t availableCount = 0;
        Vulkan::Check(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &availableCount, nullptr),
                      "query ui platform present mode count");

        ImVector<VkPresentModeKHR> availableModes;
        availableModes.resize(static_cast<int>(availableCount));
        Vulkan::Check(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &availableCount,
                                                                availableModes.Data),
                      "query ui platform present modes");

        for (int requestedIndex = 0; requestedIndex < requestModesCount; ++requestedIndex)
        {
            for (uint32_t availableIndex = 0; availableIndex < availableCount; ++availableIndex)
            {
                if (requestModes[requestedIndex] == availableModes[availableIndex])
                {
                    return requestModes[requestedIndex];
                }
            }
        }

        return VK_PRESENT_MODE_FIFO_KHR;
    }

    void DestroyPlatformFrame(VkDevice device, UiPlatformFrame& frame)
    {
        if (frame.fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, frame.fence, nullptr);
            frame.fence = VK_NULL_HANDLE;
        }
        if (frame.commandBuffer != VK_NULL_HANDLE && frame.commandPool != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(device, frame.commandPool, 1, &frame.commandBuffer);
            frame.commandBuffer = VK_NULL_HANDLE;
        }
        if (frame.commandPool != VK_NULL_HANDLE)
        {
            vkDestroyCommandPool(device, frame.commandPool, nullptr);
            frame.commandPool = VK_NULL_HANDLE;
        }
        if (frame.backbufferView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, frame.backbufferView, nullptr);
            frame.backbufferView = VK_NULL_HANDLE;
        }
        if (frame.framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device, frame.framebuffer, nullptr);
            frame.framebuffer = VK_NULL_HANDLE;
        }
    }

    void DestroyPlatformFrameSemaphores(VkDevice device, UiPlatformFrameSemaphores& semaphores)
    {
        if (semaphores.imageAcquiredSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, semaphores.imageAcquiredSemaphore, nullptr);
            semaphores.imageAcquiredSemaphore = VK_NULL_HANDLE;
        }
        if (semaphores.renderCompleteSemaphore != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, semaphores.renderCompleteSemaphore, nullptr);
            semaphores.renderCompleteSemaphore = VK_NULL_HANDLE;
        }
    }

    void CreatePlatformWindowCommandBuffers(VkDevice device, UiPlatformWindow& window, uint32_t queueFamily)
    {
        for (uint32_t imageIndex = 0; imageIndex < window.imageCount; ++imageIndex)
        {
            UiPlatformFrame& frame = window.frames[imageIndex];

            VkCommandPoolCreateInfo commandPoolInfo{};
            commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            commandPoolInfo.queueFamilyIndex = queueFamily;
            Vulkan::Check(vkCreateCommandPool(device, &commandPoolInfo, nullptr, &frame.commandPool),
                          "create ui platform viewport command pool");

            VkCommandBufferAllocateInfo commandBufferInfo{};
            commandBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            commandBufferInfo.commandPool = frame.commandPool;
            commandBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            commandBufferInfo.commandBufferCount = 1;
            Vulkan::Check(vkAllocateCommandBuffers(device, &commandBufferInfo, &frame.commandBuffer),
                          "allocate ui platform viewport command buffer");

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            Vulkan::Check(vkCreateFence(device, &fenceInfo, nullptr, &frame.fence),
                          "create ui platform viewport fence");
        }

        for (uint32_t semaphoreIndex = 0; semaphoreIndex < window.semaphoreCount; ++semaphoreIndex)
        {
            UiPlatformFrameSemaphores& semaphores = window.frameSemaphores[semaphoreIndex];
            VkSemaphoreCreateInfo semaphoreInfo{};
            semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            Vulkan::Check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphores.imageAcquiredSemaphore),
                          "create ui platform viewport acquire semaphore");
            Vulkan::Check(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &semaphores.renderCompleteSemaphore),
                          "create ui platform viewport render semaphore");
        }
    }

    void CreatePlatformWindowSwapChain(VkPhysicalDevice physicalDevice, VkDevice device, UiPlatformWindow& window,
                                       int width, int height, uint32_t minImageCount)
    {
        VkSwapchainKHR oldSwapChain = window.swapchain;
        window.swapchain = VK_NULL_HANDLE;

        Vulkan::Check(vkDeviceWaitIdle(device), "wait device idle for ui platform viewport resize");

        for (int imageIndex = 0; imageIndex < window.frames.Size; ++imageIndex)
        {
            DestroyPlatformFrame(device, window.frames[imageIndex]);
        }
        for (int semaphoreIndex = 0; semaphoreIndex < window.frameSemaphores.Size; ++semaphoreIndex)
        {
            DestroyPlatformFrameSemaphores(device, window.frameSemaphores[semaphoreIndex]);
        }
        window.frames.clear();
        window.frameSemaphores.clear();
        window.imageCount = 0;
        if (window.renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, window.renderPass, nullptr);
            window.renderPass = VK_NULL_HANDLE;
        }

        VkSurfaceCapabilitiesKHR surfaceCapabilities{};
        Vulkan::Check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, window.surface, &surfaceCapabilities),
                      "query ui platform surface capabilities");

        VkSwapchainCreateInfoKHR swapChainInfo{};
        swapChainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapChainInfo.surface = window.surface;
        swapChainInfo.minImageCount = minImageCount;
        swapChainInfo.imageFormat = window.surfaceFormat.format;
        swapChainInfo.imageColorSpace = window.surfaceFormat.colorSpace;
        swapChainInfo.imageArrayLayers = 1;
        swapChainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapChainInfo.preTransform =
            (surfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                : surfaceCapabilities.currentTransform;
        swapChainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        swapChainInfo.presentMode = window.presentMode;
        swapChainInfo.clipped = VK_TRUE;
        swapChainInfo.oldSwapchain = oldSwapChain;
        if (swapChainInfo.minImageCount < surfaceCapabilities.minImageCount)
        {
            swapChainInfo.minImageCount = surfaceCapabilities.minImageCount;
        }
        else if (surfaceCapabilities.maxImageCount != 0 &&
                 swapChainInfo.minImageCount > surfaceCapabilities.maxImageCount)
        {
            swapChainInfo.minImageCount = surfaceCapabilities.maxImageCount;
        }

        if (surfaceCapabilities.currentExtent.width == 0xffffffff)
        {
            swapChainInfo.imageExtent.width = window.width = width;
            swapChainInfo.imageExtent.height = window.height = height;
        }
        else
        {
            swapChainInfo.imageExtent.width = window.width = static_cast<int>(surfaceCapabilities.currentExtent.width);
            swapChainInfo.imageExtent.height = window.height = static_cast<int>(surfaceCapabilities.currentExtent.height);
        }

        Vulkan::Check(vkCreateSwapchainKHR(device, &swapChainInfo, nullptr, &window.swapchain),
                      "create ui platform viewport swapchain");
        Vulkan::Check(vkGetSwapchainImagesKHR(device, window.swapchain, &window.imageCount, nullptr),
                      "query ui platform viewport swapchain image count");

        std::array<VkImage, 16> backbuffers{};
        if (window.imageCount > backbuffers.size())
        {
            Throw(std::runtime_error("ui platform viewport swapchain exceeds supported image count"));
        }
        Vulkan::Check(vkGetSwapchainImagesKHR(device, window.swapchain, &window.imageCount, backbuffers.data()),
                      "query ui platform viewport swapchain images");

        window.semaphoreCount = window.imageCount + 1;
        window.frames.resize(static_cast<int>(window.imageCount));
        window.frameSemaphores.resize(static_cast<int>(window.semaphoreCount));
        std::fill_n(window.frames.Data, window.frames.Size, UiPlatformFrame{});
        std::fill_n(window.frameSemaphores.Data, window.frameSemaphores.Size, UiPlatformFrameSemaphores{});
        for (uint32_t imageIndex = 0; imageIndex < window.imageCount; ++imageIndex)
        {
            window.frames[imageIndex].backbuffer = backbuffers[imageIndex];
        }

        if (oldSwapChain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device, oldSwapChain, nullptr);
        }

        VkAttachmentDescription attachment{};
        attachment.format = window.surfaceFormat.format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = window.clearEnable ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachment{};
        colorAttachment.attachment = 0;
        colorAttachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachment;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &attachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;
        Vulkan::Check(vkCreateRenderPass(device, &renderPassInfo, nullptr, &window.renderPass),
                      "create ui platform viewport render pass");

        VkImageViewCreateInfo imageViewInfo{};
        imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.format = window.surfaceFormat.format;
        imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
        imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_G;
        imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_B;
        imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_A;
        imageViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageViewInfo.subresourceRange.levelCount = 1;
        imageViewInfo.subresourceRange.layerCount = 1;

        for (uint32_t imageIndex = 0; imageIndex < window.imageCount; ++imageIndex)
        {
            UiPlatformFrame& frame = window.frames[imageIndex];
            imageViewInfo.image = frame.backbuffer;
            Vulkan::Check(vkCreateImageView(device, &imageViewInfo, nullptr, &frame.backbufferView),
                          "create ui platform viewport image view");
        }

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = window.renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.width = static_cast<uint32_t>(window.width);
        framebufferInfo.height = static_cast<uint32_t>(window.height);
        framebufferInfo.layers = 1;

        for (uint32_t imageIndex = 0; imageIndex < window.imageCount; ++imageIndex)
        {
            UiPlatformFrame& frame = window.frames[imageIndex];
            VkImageView attachmentView = frame.backbufferView;
            framebufferInfo.pAttachments = &attachmentView;
            Vulkan::Check(vkCreateFramebuffer(device, &framebufferInfo, nullptr, &frame.framebuffer),
                          "create ui platform viewport framebuffer");
        }
    }

    void CreateOrResizePlatformWindow(VkPhysicalDevice physicalDevice, VkDevice device, UiPlatformWindow& window,
                                      uint32_t queueFamily, int width, int height, uint32_t minImageCount)
    {
        CreatePlatformWindowSwapChain(physicalDevice, device, window, width, height, minImageCount);
        CreatePlatformWindowCommandBuffers(device, window, queueFamily);
    }

    void DestroyPlatformWindow(VkInstance instance, VkDevice device, UiPlatformWindow& window)
    {
        Vulkan::Check(vkDeviceWaitIdle(device), "wait device idle for ui platform viewport destroy");

        for (int imageIndex = 0; imageIndex < window.frames.Size; ++imageIndex)
        {
            DestroyPlatformFrame(device, window.frames[imageIndex]);
        }
        for (int semaphoreIndex = 0; semaphoreIndex < window.frameSemaphores.Size; ++semaphoreIndex)
        {
            DestroyPlatformFrameSemaphores(device, window.frameSemaphores[semaphoreIndex]);
        }
        window.frames.clear();
        window.frameSemaphores.clear();

        if (window.renderPass != VK_NULL_HANDLE)
        {
            vkDestroyRenderPass(device, window.renderPass, nullptr);
        }
        if (window.swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device, window.swapchain, nullptr);
        }
        if (window.surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(instance, window.surface, nullptr);
        }

        window = UiPlatformWindow{};
    }

    std::string ExtractConsolePrefix(const std::string& input)
    {
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return {};
        }
        size_t end = input.find_first_of(" =\t\r\n", start);
        if (end == std::string::npos)
        {
            return input.substr(start);
        }
        return input.substr(start, end - start);
    }

    ImVec2 TransformUiPointToFramebuffer(const ImVec2 point, const UiPushConstants& pushConsts, const VkExtent2D& extent)
    {
        const float x = point.x * pushConsts.scale[0] + pushConsts.translate[0];
        const float y = point.y * pushConsts.scale[1] + pushConsts.translate[1];
        const float rx = x * pushConsts.rotation[0] + y * pushConsts.rotation[1];
        const float ry = x * pushConsts.rotation[2] + y * pushConsts.rotation[3];

        return ImVec2((rx * 0.5f + 0.5f) * static_cast<float>(extent.width),
                      (ry * 0.5f + 0.5f) * static_cast<float>(extent.height));
    }

    void BindUiRenderState(VkCommandBuffer commandBuffer, VkPipeline pipeline, VkPipelineLayout pipelineLayout,
                           VkDescriptorSet bindlessDescriptorSet, VkBuffer vertexBuffer, const VkViewport& viewport,
                           const VkRect2D& scissor, const UiPushConstants& pushConsts)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        if (bindlessDescriptorSet != VK_NULL_HANDLE)
        {
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                                    &bindlessDescriptorSet, 0, nullptr);
        }
        if (vertexBuffer != VK_NULL_HANDLE)
        {
            constexpr VkDeviceSize vertexOffset = 0;
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, &vertexOffset);
        }
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(UiPushConstants), &pushConsts);
    }

    VkPipeline CreateUiGraphicsPipeline(const Vulkan::Device& device, VkPipelineLayout pipelineLayout,
                                        VkRenderPass renderPass)
    {
        const Vulkan::ShaderModule vertShader(device, kUiVertexShaderPath);
        const Vulkan::ShaderModule fragShader(device, kUiFragmentShaderPath);

        VkVertexInputBindingDescription vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(UiBatchedVertex);
        vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        std::array<VkVertexInputAttributeDescription, 5> vertexAttributes{};
        vertexAttributes[0].location = 0;
        vertexAttributes[0].binding = 0;
        vertexAttributes[0].format = VK_FORMAT_R32G32_SFLOAT;
        vertexAttributes[0].offset = static_cast<uint32_t>(offsetof(UiBatchedVertex, position));
        vertexAttributes[1].location = 1;
        vertexAttributes[1].binding = 0;
        vertexAttributes[1].format = VK_FORMAT_R32G32_SFLOAT;
        vertexAttributes[1].offset = static_cast<uint32_t>(offsetof(UiBatchedVertex, uv));
        vertexAttributes[2].location = 2;
        vertexAttributes[2].binding = 0;
        vertexAttributes[2].format = VK_FORMAT_R8G8B8A8_UNORM;
        vertexAttributes[2].offset = static_cast<uint32_t>(offsetof(UiBatchedVertex, color));
        vertexAttributes[3].location = 3;
        vertexAttributes[3].binding = 0;
        vertexAttributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
        vertexAttributes[3].offset = static_cast<uint32_t>(offsetof(UiBatchedVertex, clipRect));
        vertexAttributes[4].location = 4;
        vertexAttributes[4].binding = 0;
        vertexAttributes[4].format = VK_FORMAT_R32_UINT;
        vertexAttributes[4].offset = static_cast<uint32_t>(offsetof(UiBatchedVertex, textureIndex));

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &vertexBinding;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.blendEnable = VK_TRUE;
        colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlendAttachment;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

        VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(std::size(dynamicStates));
        dynamicState.pDynamicStates = dynamicStates;

        VkPipelineShaderStageCreateInfo shaderStages[] = {
            vertShader.CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT),
            fragShader.CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT)};

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(std::size(shaderStages));
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        Vulkan::Check(vkCreateGraphicsPipelines(device.Handle(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline),
                      "create ui pipeline");
        return pipeline;
    }
} // namespace

UserInterface::UserInterface(NextEngine* engine, Vulkan::CommandPool& commandPool, const Vulkan::SwapChain& swapChain,
                             const Vulkan::DepthBuffer& depthBuffer, Runtime::Config::UserSettings& userSettings,
                             std::function<void()> funcPreConfig, std::function<void()> funcInit) :
    userSettings_(userSettings), engine_(engine)
{
    const auto& window = swapChain.Device().Surface().Instance().Window();

    renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
    renderPass_->SetDebugName("ImGui Render Pass");
    CreateUiPipeline(swapChain);

    // Initialise ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    auto& io = ImGui::GetIO();
    // No ini file.
    io.IniFilename = "imgui.ini";
    io.WantCaptureMouse = false;
    io.WantCaptureKeyboard = false;

    funcPreConfig();

    // Initialise ImGui GLFW adapter
    if (!ImGui_ImplSDL3_InitForVulkan(window.Handle()))
    {
        Throw(std::runtime_error("failed to initialise ImGui GLFW adapter"));
    }

    InitializeRendererBackend();

    // Window scaling and style.
#if ANDROID
    const float scaleFactor = 0.75f / static_cast<float>(GAndroidMagicScale);
#else
    const float scaleFactor = 1.0f;
#endif
    constexpr float fontSize = 16.0f;

    UserInterface::SetStyle();
    ImGui::GetStyle().ScaleAllSizes(scaleFactor);

    // Upload ImGui fonts (use ImGuiFreeType for better font rendering, see
    // https://github.com/ocornut/imgui/tree/master/misc/freetype).
    io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
    io.Fonts->FontBuilderFlags = ImGuiFreeTypeBuilderFlags_NoHinting;
    // const ImWchar* glyphRange = GOption->locale == "RU" ? io.Fonts->GetGlyphRangesCyrillic()
    //     : GOption->locale == "zhCN"                     ? io.Fonts->GetGlyphRangesChineseFull()
    //                                                     : io.Fonts->GetGlyphRangesDefault();

    if (!NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
            .filePath = "assets/fonts/Roboto-Regular.ttf",
            .pixelSize = fontSize * scaleFactor,
            .includeChineseFull = true,
        }))
    {
        Throw(std::runtime_error("failed to load basic ImGui Text font"));
    }

    static const ImWchar iconRange[] = {
        ICON_MIN_FA,
        ICON_MAX_FA, // Basic Latin + Latin Supplement
        0,
    };
    ImFontConfig config;
    config.MergeMode = true;
    config.GlyphMinAdvanceX = fontSize;
    config.GlyphOffset = ImVec2(0, 0);

    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-regular-400.ttf",
        .pixelSize = fontSize * scaleFactor,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .warnOnFailure = false,
    });
    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-solid-900.ttf",
        .pixelSize = fontSize * scaleFactor,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .warnOnFailure = false,
    });
    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/fa-brands-400.ttf",
        .pixelSize = fontSize * scaleFactor,
        .includeChineseFull = false,
        .glyphRanges = iconRange,
        .fontConfig = &config,
        .warnOnFailure = false,
    });

    ImFontConfig configLocale;
    configLocale.MergeMode = true;
    NextUI::FontLoader::Load(NextUI::FontLoader::FFontRequest{
        .filePath = "assets/fonts/DroidSansFallback.ttf",
        .pixelSize = (fontSize + 2.0f) * scaleFactor,
        .includeChineseFull = true,
        .fontConfig = &configLocale,
        .warnOnFailure = false,
    });

    if (funcInit != nullptr)
    {
        funcInit();
    }
    InitializeFontTexture(commandPool);
}

UserInterface::~UserInterface()
{
    ShutdownRendererBackend();
    DestroyUiPipeline();
    uiFrameBuffers_.clear();
    uiRenderBuffers_.clear();
    platformUiRenderBuffers_.clear();

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void UserInterface::OnCreateSurface(const Vulkan::SwapChain& swapChain, const Vulkan::DepthBuffer& depthBuffer)
{
    renderPass_.reset(new Vulkan::RenderPass(swapChain, depthBuffer, VK_ATTACHMENT_LOAD_OP_LOAD));
    renderPass_->SetDebugName("ImGui Render Pass");
    CreateUiPipeline(swapChain);

    for (const auto& imageView : swapChain.ImageViews())
    {
        uiFrameBuffers_.emplace_back(swapChain.Extent(), *imageView, *renderPass_, false);
    }
    uiRenderBuffers_.resize(swapChain.Images().size());
}

void UserInterface::OnDestroySurface()
{
    DestroyUiPipeline();
    renderPass_.reset();
    uiFrameBuffers_.clear();
    uiRenderBuffers_.clear();
    platformUiRenderBuffers_.clear();
}

ImTextureID UserInterface::EncodeBindlessTextureId(uint32_t textureIndex)
{
    return (ImTextureID)(static_cast<intptr_t>(textureIndex + 1u));
}

bool UserInterface::DecodeBindlessTextureId(ImTextureID textureId, uint32_t& outTextureIndex)
{
    const uint64_t rawValue = static_cast<uint64_t>((intptr_t)textureId);
    if (rawValue == 0)
    {
        return false;
    }

    const uint64_t textureIndex = rawValue - 1u;
    const auto* texturePool = Assets::GlobalTexturePool::GetInstance();
    if (texturePool == nullptr || textureIndex >= texturePool->TotalTextures())
    {
        return false;
    }

    outTextureIndex = static_cast<uint32_t>(textureIndex);
    return true;
}

void UserInterface::InitializeRendererBackend()
{
    auto& io = ImGui::GetIO();
    if (io.BackendRendererUserData != nullptr)
    {
        Throw(std::runtime_error("imgui renderer backend already initialized"));
    }

    io.BackendRendererUserData = this;
    io.BackendRendererName = "gk_imgui_renderer";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasViewports;

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        if (platformIo.Platform_CreateVkSurface == nullptr)
        {
            Throw(std::runtime_error("imgui platform backend does not provide Platform_CreateVkSurface"));
        }
    }

    platformIo.Renderer_CreateWindow = &UserInterface::CreatePlatformViewportWindowCallback;
    platformIo.Renderer_DestroyWindow = &UserInterface::DestroyPlatformViewportWindowCallback;
    platformIo.Renderer_SetWindowSize = &UserInterface::ResizePlatformViewportWindowCallback;
    platformIo.Renderer_RenderWindow = &UserInterface::RenderPlatformViewportWindowCallback;
    platformIo.Renderer_SwapBuffers = &UserInterface::SwapPlatformViewportBuffersCallback;

    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (mainViewport->RendererUserData == nullptr)
    {
        mainViewport->RendererUserData = IM_NEW(UiPlatformViewportData)();
    }
}

void UserInterface::ShutdownRendererBackend()
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    auto& io = ImGui::GetIO();
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    ImGui::DestroyPlatformWindows();

    if (ImGuiViewport* mainViewport = ImGui::GetMainViewport(); mainViewport != nullptr)
    {
        if (mainViewport->RendererUserData != nullptr)
        {
            IM_DELETE(static_cast<UiPlatformViewportData*>(mainViewport->RendererUserData));
            mainViewport->RendererUserData = nullptr;
        }
    }

    platformIo.Renderer_CreateWindow = nullptr;
    platformIo.Renderer_DestroyWindow = nullptr;
    platformIo.Renderer_SetWindowSize = nullptr;
    platformIo.Renderer_RenderWindow = nullptr;
    platformIo.Renderer_SwapBuffers = nullptr;
    platformIo.Renderer_RenderState = nullptr;

    io.BackendRendererName = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags &= ~(ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasViewports);
}

void UserInterface::BeginRendererBackendFrame() {}

void UserInterface::InitializeFontTexture(Vulkan::CommandPool& commandPool)
{
    auto& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    if (pixels == nullptr || width <= 0 || height <= 0)
    {
        Throw(std::runtime_error("failed to build imgui font atlas"));
    }

    const uint32_t fontTextureSize = static_cast<uint32_t>(width * height * 4);
    auto fontTexture = std::make_unique<Assets::TextureImage>(
        commandPool, static_cast<size_t>(width), static_cast<size_t>(height), 1, VK_FORMAT_R8G8B8A8_UNORM, pixels,
        fontTextureSize);
    fontTexture->MainThreadPostLoading(commandPool);
    fontTexture->SetDebugName(kUiFontAtlasTextureName);

    auto* texturePool = Assets::GlobalTexturePool::GetInstance();
    if (texturePool == nullptr)
    {
        Throw(std::runtime_error("global texture pool is unavailable for imgui font atlas"));
    }
    
    fontTextureIndex_ = texturePool->RegisterTexture(
        kUiFontAtlasTextureName, std::move(fontTexture), Assets::ETextureLifetime::ETL_Persistent);

    io.Fonts->TexID = EncodeBindlessTextureId(fontTextureIndex_);
}

ImTextureID UserInterface::RequestImTextureId(uint32_t globalTextureId)
{
    if (Assets::GlobalTexturePool::GetTextureImage(globalTextureId) == nullptr)
    {
        return 0;
    }

    return EncodeBindlessTextureId(globalTextureId);
}

ImTextureID UserInterface::RequestImTextureByName(const std::string& name)
{
    uint32_t id = Assets::GlobalTexturePool::GetTextureIndexByName(name);
    if (id == static_cast<uint32_t>(-1))
    {
        return 0;
    }
    return RequestImTextureId(id);
}

UserInterface::FUiTextureHandle UserInterface::RequestUiTexture(const std::string& path, bool srgb)
{
    FUiTextureHandle handle{};
    if (path.empty() || !Utilities::FileHelper::IsAssetAvailable(path))
    {
        return handle;
    }

    if (uiTextureLoadRequests_.insert(path).second)
    {
        Assets::GlobalTexturePool::LoadTexture(path, srgb);
    }

    handle.textureId = RequestImTextureByName(path);
    handle.valid = handle.textureId != 0;

    if (const auto sizeIt = uiTexturePixelSizeCache_.find(path); sizeIt != uiTexturePixelSizeCache_.end())
    {
        handle.pixelSize = sizeIt->second;
        return handle;
    }

    int width = 0;
    int height = 0;
    int comp = 0;
    const std::string platformPath = Utilities::FileHelper::GetPlatformFilePath(path.c_str());
    if (stbi_info(platformPath.c_str(), &width, &height, &comp) != 0 && width > 0 && height > 0)
    {
        handle.pixelSize = ImVec2(static_cast<float>(width), static_cast<float>(height));
    }
    uiTexturePixelSizeCache_[path] = handle.pixelSize;
    return handle;
}

void UserInterface::CreateUiPipeline(const Vulkan::SwapChain& swapChain)
{
    DestroyUiPipeline();
    if (renderPass_ == nullptr)
    {
        return;
    }

    const auto& device = swapChain.Device();
    const auto* texturePool = Assets::GlobalTexturePool::GetInstance();
    if (texturePool == nullptr)
    {
        Throw(std::runtime_error("global texture pool is unavailable for ui pipeline"));
    }

    const VkDescriptorSetLayout bindlessLayout = texturePool->Layout();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(UiPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &bindlessLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    Vulkan::Check(vkCreatePipelineLayout(device.Handle(), &pipelineLayoutInfo, nullptr, &uiPipelineLayout_),
                  "create ui pipeline layout");
    uiPipeline_ = CreateUiGraphicsPipeline(device, uiPipelineLayout_, renderPass_->Handle());
}

void UserInterface::DestroyUiPipeline()
{
    if (engine_ == nullptr)
    {
        return;
    }

    const auto& device = engine_->GetRenderer().Device();
    if (uiPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device.Handle(), uiPipeline_, nullptr);
        uiPipeline_ = VK_NULL_HANDLE;
    }
    if (uiPlatformViewportPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device.Handle(), uiPlatformViewportPipeline_, nullptr);
        uiPlatformViewportPipeline_ = VK_NULL_HANDLE;
    }
    uiPlatformViewportRenderPass_ = VK_NULL_HANDLE;
    if (uiPipelineLayout_ != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device.Handle(), uiPipelineLayout_, nullptr);
        uiPipelineLayout_ = VK_NULL_HANDLE;
    }
}

VkPipeline UserInterface::GetOrCreatePlatformViewportPipeline(VkRenderPass renderPass)
{
    if (renderPass == VK_NULL_HANDLE)
    {
        return VK_NULL_HANDLE;
    }

    if (uiPlatformViewportPipeline_ != VK_NULL_HANDLE && uiPlatformViewportRenderPass_ == renderPass)
    {
        return uiPlatformViewportPipeline_;
    }

    const auto& device = engine_->GetRenderer().Device();
    if (uiPlatformViewportPipeline_ != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device.Handle(), uiPlatformViewportPipeline_, nullptr);
        uiPlatformViewportPipeline_ = VK_NULL_HANDLE;
    }

    uiPlatformViewportPipeline_ = CreateUiGraphicsPipeline(device, uiPipelineLayout_, renderPass);
    uiPlatformViewportRenderPass_ = renderPass;
    return uiPlatformViewportPipeline_;
}

void UserInterface::RenderDrawData(ImDrawData* drawData, VkCommandBuffer commandBuffer, FUiRenderBuffers& renderBuffers,
                                   VkExtent2D framebufferExtent, bool hdrOutput, VkPipeline pipeline)
{
    if (drawData == nullptr || drawData->CmdListsCount <= 0 || pipeline == VK_NULL_HANDLE)
    {
        return;
    }
    if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
    {
        return;
    }
    if (framebufferExtent.width == 0 || framebufferExtent.height == 0)
    {
        return;
    }

    UiPushConstants pushConsts{};
    pushConsts.scale[0] = 2.0f / drawData->DisplaySize.x;
    pushConsts.scale[1] = 2.0f / drawData->DisplaySize.y;
    pushConsts.translate[0] = -1.0f - drawData->DisplayPos.x * pushConsts.scale[0];
    pushConsts.translate[1] = -1.0f - drawData->DisplayPos.y * pushConsts.scale[1];
    pushConsts.rotation[0] = 1.0f;
    pushConsts.rotation[1] = 0.0f;
    pushConsts.rotation[2] = 0.0f;
    pushConsts.rotation[3] = 1.0f;
#if ANDROID
    pushConsts.rotation[0] = 0.0f;
    pushConsts.rotation[1] = 1.0f;
    pushConsts.rotation[2] = -1.0f;
    pushConsts.rotation[3] = 0.0f;
#endif
    pushConsts.hdrOutput = hdrOutput ? 1u : 0u;
    pushConsts.hdrReferenceWhiteNit = kUiHdrReferenceWhiteNit;

    std::vector<UiBatchedVertex> batchedVertices;
    batchedVertices.reserve(static_cast<size_t>(std::max(drawData->TotalIdxCount, 0)));

    std::vector<UiDrawOp> drawOps;
    drawOps.reserve(static_cast<size_t>(drawData->CmdListsCount) * 2);

    auto FlushPendingDraw = [&](uint32_t& segmentStartVertex)
    {
        const uint32_t vertexCount = static_cast<uint32_t>(batchedVertices.size()) - segmentStartVertex;
        if (vertexCount == 0)
        {
            return;
        }

        drawOps.push_back(
            UiDrawOp{UiDrawOp::EType::Draw, UiDrawSegment{segmentStartVertex, vertexCount}, nullptr, nullptr});
        segmentStartVertex = static_cast<uint32_t>(batchedVertices.size());
    };

    uint32_t currentSegmentStartVertex = 0;
    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
    {
        const ImDrawList* drawList = drawData->CmdLists[listIndex];
        if (drawList == nullptr)
        {
            continue;
        }

        for (int cmdIndex = 0; cmdIndex < drawList->CmdBuffer.Size; ++cmdIndex)
        {
            const ImDrawCmd* drawCmd = &drawList->CmdBuffer[cmdIndex];
            if (drawCmd->UserCallback != nullptr)
            {
                FlushPendingDraw(currentSegmentStartVertex);
                drawOps.push_back(UiDrawOp{UiDrawOp::EType::Callback, UiDrawSegment{}, drawList, drawCmd});
                continue;
            }

            uint32_t textureIndex = fontTextureIndex_;
            if (!DecodeBindlessTextureId(drawCmd->GetTexID(), textureIndex))
            {
                continue;
            }

            const ImVec2 corners[] = {
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.x, drawCmd->ClipRect.y), pushConsts,
                                              framebufferExtent),
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.z, drawCmd->ClipRect.y), pushConsts,
                                              framebufferExtent),
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.z, drawCmd->ClipRect.w), pushConsts,
                                              framebufferExtent),
                TransformUiPointToFramebuffer(ImVec2(drawCmd->ClipRect.x, drawCmd->ClipRect.w), pushConsts,
                                              framebufferExtent),
            };

            float clipMinX = corners[0].x;
            float clipMinY = corners[0].y;
            float clipMaxX = corners[0].x;
            float clipMaxY = corners[0].y;
            for (const ImVec2& corner : corners)
            {
                clipMinX = std::min(clipMinX, corner.x);
                clipMinY = std::min(clipMinY, corner.y);
                clipMaxX = std::max(clipMaxX, corner.x);
                clipMaxY = std::max(clipMaxY, corner.y);
            }

            clipMinX = std::clamp(clipMinX, 0.0f, static_cast<float>(framebufferExtent.width));
            clipMinY = std::clamp(clipMinY, 0.0f, static_cast<float>(framebufferExtent.height));
            clipMaxX = std::clamp(clipMaxX, 0.0f, static_cast<float>(framebufferExtent.width));
            clipMaxY = std::clamp(clipMaxY, 0.0f, static_cast<float>(framebufferExtent.height));
            if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
            {
                continue;
            }

            for (uint32_t elemIndex = 0; elemIndex < drawCmd->ElemCount; ++elemIndex)
            {
                const uint32_t vertexIndex = static_cast<uint32_t>(drawList->IdxBuffer[drawCmd->IdxOffset + elemIndex]) +
                                             drawCmd->VtxOffset;
                if (vertexIndex >= static_cast<uint32_t>(drawList->VtxBuffer.Size))
                {
                    continue;
                }

                const ImDrawVert& sourceVertex = drawList->VtxBuffer[vertexIndex];
                UiBatchedVertex& batchedVertex = batchedVertices.emplace_back();
                batchedVertex.position = sourceVertex.pos;
                batchedVertex.uv = sourceVertex.uv;
                batchedVertex.color = sourceVertex.col;
                batchedVertex.clipRect[0] = clipMinX;
                batchedVertex.clipRect[1] = clipMinY;
                batchedVertex.clipRect[2] = clipMaxX;
                batchedVertex.clipRect[3] = clipMaxY;
                batchedVertex.textureIndex = textureIndex;
            }
        }
    }
    FlushPendingDraw(currentSegmentStartVertex);

    const auto& device = engine_->GetRenderer().Device();
    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(batchedVertices.size()) * sizeof(UiBatchedVertex);
    if (vertexSize > 0)
    {
        if (!renderBuffers.vertexBuffer || renderBuffers.vertexBufferSize < vertexSize)
        {
            renderBuffers.vertexBuffer.reset(new Vulkan::Buffer(device, vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT));
            renderBuffers.vertexBufferMemory.reset(new Vulkan::DeviceMemory(
                renderBuffers.vertexBuffer->AllocateMemory(
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
            renderBuffers.vertexBufferSize = vertexSize;
        }

        void* mappedData = renderBuffers.vertexBufferMemory->Map(0, vertexSize);
        memcpy(mappedData, batchedVertices.data(), static_cast<size_t>(vertexSize));
        renderBuffers.vertexBufferMemory->Unmap();
    }

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(framebufferExtent.width);
    viewport.height = static_cast<float>(framebufferExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = framebufferExtent;

    const VkDescriptorSet bindlessDescriptorSet = Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0);
    const VkBuffer vertexBufferHandle =
        renderBuffers.vertexBuffer ? renderBuffers.vertexBuffer->Handle() : VK_NULL_HANDLE;
    BindUiRenderState(commandBuffer, pipeline, uiPipelineLayout_, bindlessDescriptorSet, vertexBufferHandle,
                      viewport, scissor, pushConsts);

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    UiRendererRenderState renderState{};
    renderState.commandBuffer = commandBuffer;
    renderState.pipeline = pipeline;
    renderState.pipelineLayout = uiPipelineLayout_;
    platformIo.Renderer_RenderState = &renderState;

    for (const UiDrawOp& drawOp : drawOps)
    {
        if (drawOp.type == UiDrawOp::EType::Draw)
        {
            if (drawOp.segment.vertexCount > 0)
            {
                vkCmdDraw(commandBuffer, drawOp.segment.vertexCount, 1, drawOp.segment.vertexOffset, 0);
            }
            continue;
        }

        if (drawOp.drawCmd == nullptr || drawOp.drawCmd->UserCallback == nullptr)
        {
            continue;
        }

        if (drawOp.drawCmd->UserCallback == ImDrawCallback_ResetRenderState)
        {
            BindUiRenderState(commandBuffer, pipeline, uiPipelineLayout_, bindlessDescriptorSet, vertexBufferHandle,
                              viewport, scissor, pushConsts);
            continue;
        }

        drawOp.drawCmd->UserCallback(drawOp.drawList, drawOp.drawCmd);
        BindUiRenderState(commandBuffer, pipeline, uiPipelineLayout_, bindlessDescriptorSet, vertexBufferHandle,
                          viewport, scissor, pushConsts);
    }

    platformIo.Renderer_RenderState = nullptr;
}

UserInterface* UserInterface::GetRendererBackendOwner()
{
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return nullptr;
    }

    return static_cast<UserInterface*>(ImGui::GetIO().BackendRendererUserData);
}

void UserInterface::CreatePlatformViewportWindowCallback(ImGuiViewport* viewport)
{
    if (UserInterface* owner = GetRendererBackendOwner(); owner != nullptr)
    {
        owner->CreatePlatformViewportWindow(viewport);
    }
}

void UserInterface::DestroyPlatformViewportWindowCallback(ImGuiViewport* viewport)
{
    if (UserInterface* owner = GetRendererBackendOwner(); owner != nullptr)
    {
        owner->DestroyPlatformViewportWindow(viewport);
    }
}

void UserInterface::ResizePlatformViewportWindowCallback(ImGuiViewport* viewport, ImVec2 size)
{
    if (UserInterface* owner = GetRendererBackendOwner(); owner != nullptr)
    {
        owner->ResizePlatformViewportWindow(viewport, size);
    }
}

void UserInterface::RenderPlatformViewportWindowCallback(ImGuiViewport* viewport, void* renderArg)
{
    UserInterface* owner = renderArg != nullptr ? static_cast<UserInterface*>(renderArg) : GetRendererBackendOwner();
    if (owner != nullptr)
    {
        owner->RenderPlatformViewportWindow(viewport);
    }
}

void UserInterface::SwapPlatformViewportBuffersCallback(ImGuiViewport* viewport, void* renderArg)
{
    UserInterface* owner = renderArg != nullptr ? static_cast<UserInterface*>(renderArg) : GetRendererBackendOwner();
    if (owner != nullptr)
    {
        owner->SwapPlatformViewportBuffers(viewport);
    }
}

void UserInterface::CreatePlatformViewportWindow(ImGuiViewport* viewport)
{
    if (viewport == nullptr)
    {
        return;
    }

    auto* viewportData = IM_NEW(UiPlatformViewportData)();
    viewport->RendererUserData = viewportData;

    UiPlatformWindow& window = viewportData->window;
    const auto& renderer = engine_->GetRenderer();
    const auto& device = renderer.Device();

    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    VkResult result = static_cast<VkResult>(platformIo.Platform_CreateVkSurface(
        viewport, reinterpret_cast<ImU64>(device.Surface().Instance().Handle()), nullptr,
        reinterpret_cast<ImU64*>(&window.surface)));
    Vulkan::Check(result, "create ui platform viewport surface");

    VkBool32 presentSupported = VK_FALSE;
    Vulkan::Check(vkGetPhysicalDeviceSurfaceSupportKHR(device.PhysicalDevice(), device.GraphicsFamilyIndex(),
                                                       window.surface, &presentSupported),
                  "query ui platform viewport present support");
    if (presentSupported != VK_TRUE)
    {
        Throw(std::runtime_error("ui platform viewport surface has no present support"));
    }

    const VkFormat requestedFormats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM};
    window.surfaceFormat = SelectPlatformSurfaceFormat(device.PhysicalDevice(), window.surface, requestedFormats,
                                                       static_cast<int>(std::size(requestedFormats)),
                                                       VK_COLORSPACE_SRGB_NONLINEAR_KHR);

    const VkPresentModeKHR requestedPresentModes[] = {
        VK_PRESENT_MODE_MAILBOX_KHR,
        VK_PRESENT_MODE_IMMEDIATE_KHR,
        VK_PRESENT_MODE_FIFO_KHR};
    window.presentMode = SelectPlatformPresentMode(device.PhysicalDevice(), window.surface, requestedPresentModes,
                                                   static_cast<int>(std::size(requestedPresentModes)));
    window.clearEnable = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) == 0;
    window.useDynamicRendering = false;

    CreateOrResizePlatformWindow(device.PhysicalDevice(), device.Handle(), window, device.GraphicsFamilyIndex(),
                                 static_cast<int>(viewport->Size.x),
                                 static_cast<int>(viewport->Size.y), renderer.SwapChain().MinImageCount());
    viewportData->windowOwned = true;
}

void UserInterface::DestroyPlatformViewportWindow(ImGuiViewport* viewport)
{
    if (viewport == nullptr || viewport->RendererUserData == nullptr)
    {
        return;
    }

    auto* viewportData = static_cast<UiPlatformViewportData*>(viewport->RendererUserData);
    if (viewportData->windowOwned)
    {
        const auto& device = engine_->GetRenderer().Device();
        DestroyPlatformWindow(device.Surface().Instance().Handle(), device.Handle(), viewportData->window);
    }

    platformUiRenderBuffers_.erase(viewport->ID);
    IM_DELETE(viewportData);
    viewport->RendererUserData = nullptr;
}

void UserInterface::ResizePlatformViewportWindow(ImGuiViewport* viewport, ImVec2 size)
{
    if (viewport == nullptr || viewport->RendererUserData == nullptr)
    {
        return;
    }

    auto* viewportData = static_cast<UiPlatformViewportData*>(viewport->RendererUserData);
    UiPlatformWindow& window = viewportData->window;
    const auto& renderer = engine_->GetRenderer();
    const auto& device = renderer.Device();

    window.clearEnable = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) == 0;
    CreateOrResizePlatformWindow(device.PhysicalDevice(), device.Handle(), window, device.GraphicsFamilyIndex(),
                                 static_cast<int>(size.x), static_cast<int>(size.y),
                                 renderer.SwapChain().MinImageCount());
    viewportData->swapChainNeedRebuild = false;
    viewportData->swapChainSuboptimal = false;
}

void UserInterface::RenderPlatformViewportWindow(ImGuiViewport* viewport)
{
    if (viewport == nullptr || viewport->RendererUserData == nullptr || viewport->DrawData == nullptr)
    {
        return;
    }

    auto* viewportData = static_cast<UiPlatformViewportData*>(viewport->RendererUserData);
    UiPlatformWindow& window = viewportData->window;
    const auto& renderer = engine_->GetRenderer();
    const auto& device = renderer.Device();
    VkResult result = VK_SUCCESS;

    if (viewportData->swapChainNeedRebuild || viewportData->swapChainSuboptimal)
    {
        ResizePlatformViewportWindow(viewport, viewport->Size);
    }

    UiPlatformFrameSemaphores& frameSemaphores = window.frameSemaphores[window.semaphoreIndex];
    result = vkAcquireNextImageKHR(device.Handle(), window.swapchain, UINT64_MAX,
                                   frameSemaphores.imageAcquiredSemaphore, VK_NULL_HANDLE, &window.frameIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        viewportData->swapChainNeedRebuild = true;
        return;
    }
    if (result == VK_SUBOPTIMAL_KHR)
    {
        viewportData->swapChainSuboptimal = true;
    }
    else
    {
        Vulkan::Check(result, "acquire ui platform viewport image");
    }

    UiPlatformFrame& frame = window.frames[window.frameIndex];
    for (;;)
    {
        result = vkWaitForFences(device.Handle(), 1, &frame.fence, VK_TRUE, 100);
        if (result == VK_SUCCESS)
        {
            break;
        }
        if (result != VK_TIMEOUT)
        {
            Vulkan::Check(result, "wait ui platform viewport fence");
        }
    }

    Vulkan::Check(vkResetCommandPool(device.Handle(), frame.commandPool, 0), "reset ui platform viewport command pool");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    Vulkan::Check(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "begin ui platform viewport command buffer");

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = window.renderPass;
    renderPassBeginInfo.framebuffer = frame.framebuffer;
    renderPassBeginInfo.renderArea.extent.width = static_cast<uint32_t>(window.width);
    renderPassBeginInfo.renderArea.extent.height = static_cast<uint32_t>(window.height);
    renderPassBeginInfo.clearValueCount = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) ? 0u : 1u;
    renderPassBeginInfo.pClearValues = (viewport->Flags & ImGuiViewportFlags_NoRendererClear) ? nullptr : &window.clearValue;
    vkCmdBeginRenderPass(frame.commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkPipeline viewportPipeline = GetOrCreatePlatformViewportPipeline(window.renderPass);
    auto& viewportRenderBuffers = platformUiRenderBuffers_[viewport->ID];
    if (viewportRenderBuffers.size() < window.imageCount)
    {
        viewportRenderBuffers.resize(window.imageCount);
    }

    RenderDrawData(viewport->DrawData, frame.commandBuffer, viewportRenderBuffers[window.frameIndex],
                   VkExtent2D{static_cast<uint32_t>(window.width), static_cast<uint32_t>(window.height)}, false,
                   viewportPipeline);

    vkCmdEndRenderPass(frame.commandBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frameSemaphores.imageAcquiredSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &frameSemaphores.renderCompleteSemaphore;

    Vulkan::Check(vkEndCommandBuffer(frame.commandBuffer), "end ui platform viewport command buffer");
    Vulkan::Check(vkResetFences(device.Handle(), 1, &frame.fence), "reset ui platform viewport fence");
    Vulkan::Check(vkQueueSubmit(device.GraphicsQueue(), 1, &submitInfo, frame.fence),
                  "submit ui platform viewport command buffer");
}

void UserInterface::SwapPlatformViewportBuffers(ImGuiViewport* viewport)
{
    if (viewport == nullptr || viewport->RendererUserData == nullptr)
    {
        return;
    }

    auto* viewportData = static_cast<UiPlatformViewportData*>(viewport->RendererUserData);
    if (viewportData->swapChainNeedRebuild)
    {
        return;
    }

    UiPlatformWindow& window = viewportData->window;
    UiPlatformFrameSemaphores& frameSemaphores = window.frameSemaphores[window.semaphoreIndex];
    uint32_t presentIndex = window.frameIndex;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &frameSemaphores.renderCompleteSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &window.swapchain;
    presentInfo.pImageIndices = &presentIndex;

    VkResult result = vkQueuePresentKHR(engine_->GetRenderer().Device().GraphicsQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        viewportData->swapChainNeedRebuild = true;
        return;
    }
    if (result == VK_SUBOPTIMAL_KHR)
    {
        viewportData->swapChainSuboptimal = true;
    }
    else
    {
        Vulkan::Check(result, "present ui platform viewport");
    }

    window.semaphoreIndex = (window.semaphoreIndex + 1) % window.semaphoreCount;
}

void UserInterface::PrunePlatformViewportRenderBuffers()
{
    std::unordered_set<ImGuiID> activeViewportIds;
    ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    for (int viewportIndex = 0; viewportIndex < platformIo.Viewports.Size; ++viewportIndex)
    {
        ImGuiViewport* viewport = platformIo.Viewports[viewportIndex];
        if (viewport == nullptr || viewport == mainViewport)
        {
            continue;
        }
        activeViewportIds.insert(viewport->ID);
    }

    for (auto it = platformUiRenderBuffers_.begin(); it != platformUiRenderBuffers_.end();)
    {
        if (!activeViewportIds.contains(it->first))
        {
            it = platformUiRenderBuffers_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void UserInterface::SetStyle()
{
    // NOTE: Do not override io.IniFilename here.
    // The app/editor is responsible for choosing its ini file in the PreConfig hook.
    NextUI::Theme::ApplyProfessionalTheme();
}

void UserInterface::DrawPoint(float x, float y, float size, glm::vec4 color)
{
    // in viewport mode, the start from the display
    auxDrawRequest_.push_back(
        [=]()
        {
            ImVec2 startPos = ImGui::GetMainViewport()->Pos;
            ImGui::GetBackgroundDrawList()->AddRectFilled(startPos + ImVec2{x - size, y - size},
                                                          startPos + ImVec2{x + size, y + size},
                                                          Utilities::UI::Vec4ToImU32(color));
        });
}

void UserInterface::DrawLine(float fromx, float fromy, float tox, float toy, float size, glm::vec4 color)
{
    auxDrawRequest_.push_back(
        [=]()
        {
            ImVec2 startPos = ImGui::GetMainViewport()->Pos;
            ImGui::GetBackgroundDrawList()->AddLine(startPos + ImVec2(fromx, fromy), startPos + ImVec2(tox, toy),
                                                    Utilities::UI::Vec4ToImU32(color), size);
        });
}

void UserInterface::SubmitConsoleCommand(const std::string& command)
{
    if (command.empty())
    {
        return;
    }

    spdlog::info("> {}", command);

    consoleHistory_.push_back(command);
    constexpr size_t kConsoleHistoryLimit = 128;
    if (consoleHistory_.size() > kConsoleHistoryLimit)
    {
        consoleHistory_.erase(consoleHistory_.begin(),
                              consoleHistory_.begin() + static_cast<std::ptrdiff_t>(consoleHistory_.size() - kConsoleHistoryLimit));
    }
    consoleHistoryIndex_ = static_cast<int>(consoleHistory_.size());

    const auto result = GetEngine().GetCVarSystem().ExecuteCommand(command);
    if (!result.message.empty())
    {
        if (!result.success)
        {
            spdlog::error("{}", result.message);
        }
        else
        {
            spdlog::info("{}", result.message);
        }
    }

    for (const auto& line : result.output)
    {
        spdlog::info("  {}", line);
    }

    consoleScrollToBottom_ = true;
}

void UserInterface::RefreshConsoleMatches(size_t matchLimit)
{
    if (consoleInput_ != consoleLastInput_)
    {
        consoleLastInput_ = consoleInput_;
        consoleMatchIndex_ = 0;
        consoleCompletionBase_.clear();
        consoleHistoryIndex_ = static_cast<int>(consoleHistory_.size());
    }

    std::string matchBase = consoleCompletionBase_.empty() ? ExtractConsolePrefix(consoleInput_) : consoleCompletionBase_;
    if (!matchBase.empty())
    {
        consoleMatches_ = GetEngine().GetCVarSystem().Match(matchBase, {.limit = matchLimit});
    }
    else
    {
        consoleMatches_.clear();
    }
}

void UserInterface::DrawConsoleMatchPopup(float width, const char* popupId)
{
    if (popupId == nullptr || consoleMatches_.empty() || !ImGui::IsItemActive())
    {
        return;
    }

    const ImVec2 itemMin = ImGui::GetItemRectMin();
    const ImVec2 itemMax = ImGui::GetItemRectMax();
    const float itemWidth = (width > 0.0f) ? width : (itemMax.x - itemMin.x);
    const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
    const float popupHeight = std::min(rowHeight * (static_cast<float>(consoleMatches_.size()) + 1.5f), rowHeight * 9.0f);
    const float offset = 2.0f;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float viewportBottom = viewport->Pos.y + viewport->Size.y;
    const float yBelow = itemMax.y + offset;
    const float yAbove = itemMin.y - popupHeight - offset;
    const float popupY = (yBelow + popupHeight <= viewportBottom) ? yBelow : std::max(viewport->Pos.y + offset, yAbove);

    ImGui::SetNextWindowPos(ImVec2(itemMin.x, popupY));
    ImGui::SetNextWindowSize(ImVec2(itemWidth, popupHeight));
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGuiWindowFlags popupFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
    if (ImGui::Begin(popupId, nullptr, popupFlags))
    {
        ImGui::Text("Matches:");
        ImGui::Separator();
        for (const auto& name : consoleMatches_)
        {
            ImGui::TextUnformatted(name.c_str());
        }
    }
    ImGui::End();
}

bool UserInterface::DrawConsoleCommandInput(
    const char* label, const char* hint, float width, bool closeConsoleOnSubmit, bool showMatchPopup,
    const char* matchPopupId, bool refreshMatches)
{
    constexpr size_t kMatchLimit = 8;
    if (refreshMatches)
    {
        RefreshConsoleMatches(kMatchLimit);
    }

    ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory |
        ImGuiInputTextFlags_CallbackCompletion | ImGuiInputTextFlags_CallbackEdit;
    if (width > 0.0f)
    {
        ImGui::SetNextItemWidth(width);
    }

    const bool executeCommand =
        ImGui::InputTextWithHint(label, hint, &consoleInput_, inputFlags, &UserInterface::ConsoleInputTextCallback, this);
    if (showMatchPopup)
    {
        DrawConsoleMatchPopup(width, matchPopupId);
    }

    if (!executeCommand || consoleInput_.empty())
    {
        return false;
    }

    SubmitConsoleCommand(consoleInput_);
    consoleInput_.clear();
    consoleLastInput_.clear();
    consoleMatches_.clear();
    consoleCompletionBase_.clear();
    consoleMatchIndex_ = 0;
    if (closeConsoleOnSubmit)
    {
        showConsole_ = false;
        requestConsoleFocus_ = false;
    }
    return true;
}

int UserInterface::ConsoleInputTextCallback(ImGuiInputTextCallbackData* data)
{
    auto* ui = static_cast<UserInterface*>(data->UserData);
    if (ui == nullptr)
    {
        return 0;
    }
    return ui->HandleConsoleInputTextCallback(data);
}

int UserInterface::HandleConsoleInputTextCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
    {
        if (consoleSkipEditReset_)
        {
            consoleSkipEditReset_ = false;
            return 0;
        }
        consoleCompletionBase_.clear();
        consoleMatchIndex_ = 0;
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
    {
        if (data->EventKey == ImGuiKey_UpArrow)
        {
            if (consoleHistoryIndex_ > 0)
            {
                consoleHistoryIndex_--;
            }
            else if (!consoleHistory_.empty())
            {
                consoleHistoryIndex_ = 0;
            }
        }
        else if (data->EventKey == ImGuiKey_DownArrow)
        {
            if (consoleHistoryIndex_ + 1 < static_cast<int>(consoleHistory_.size()))
            {
                consoleHistoryIndex_++;
            }
            else
            {
                consoleHistoryIndex_ = static_cast<int>(consoleHistory_.size());
            }
        }

        std::string historyCmd;
        if (!consoleHistory_.empty() && consoleHistoryIndex_ >= 0 &&
            consoleHistoryIndex_ < static_cast<int>(consoleHistory_.size()))
        {
            historyCmd = consoleHistory_[consoleHistoryIndex_];
        }

        data->DeleteChars(0, data->BufTextLen);
        if (!historyCmd.empty())
        {
            data->InsertChars(0, historyCmd.c_str());
        }
        return 0;
    }

    if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
    {
        std::string buffer(data->Buf, data->BufTextLen);
        size_t start = buffer.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
        {
            return 0;
        }
        size_t end = buffer.find_first_of(" =\t\r\n", start);
        if (end == std::string::npos)
        {
            end = buffer.size();
        }
        if (data->CursorPos > static_cast<int>(end))
        {
            return 0;
        }

        std::string prefix = buffer.substr(start, end - start);
        if (prefix.empty())
        {
            return 0;
        }

        if (consoleCompletionBase_.empty())
        {
            consoleCompletionBase_ = prefix;
            consoleMatchIndex_ = 0;
        }

        constexpr size_t kMatchLimit = 8;
        auto matches = GetEngine().GetCVarSystem().Match(consoleCompletionBase_, {.limit = kMatchLimit});
        consoleMatches_ = matches;
        if (matches.empty())
        {
            return 0;
        }

        int index = consoleMatchIndex_ % static_cast<int>(matches.size());
        const std::string& match = matches[index];
        consoleMatchIndex_ = (index + 1) % static_cast<int>(matches.size());

        std::string rest = end < buffer.size() ? buffer.substr(end) : "";
        std::string newBuffer = match + rest;

        consoleSkipEditReset_ = true;
        data->DeleteChars(0, data->BufTextLen);
        data->InsertChars(0, newBuffer.c_str());
        data->CursorPos = static_cast<int>(match.size());
    }

    return 0;
}

void UserInterface::DrawConsoleLogOutput(const char* childId, const ImVec2& size, bool bordered)
{
    DrawConsoleLogOutputInternal(childId, size, bordered);
}

void UserInterface::DrawConsoleLogOutputInternal(const char* childId, const ImVec2& size, bool bordered)
{
    const auto logSink = Runtime::Editor::GetConsoleLogSink();
    const std::vector<spdlog::details::log_msg_buffer> lines = logSink ? logSink->last_raw() : std::vector<spdlog::details::log_msg_buffer>{};
    const uint64_t revision = Runtime::Editor::GetConsoleLogSequence();
    static ImGuiTextFilter consoleFilter;
    static bool showInfo = true;
    static bool showWarn = true;
    static bool showError = true;
    static bool showDebug = true;
    static size_t clearedLineOffset = 0;

    if (clearedLineOffset > lines.size())
    {
        clearedLineOffset = 0;
    }

    auto LevelInfo = [](spdlog::level::level_enum level) -> std::pair<const char*, ImVec4>
    {
        switch (level)
        {
        case spdlog::level::trace:
        case spdlog::level::debug:
            return {"[Debug]", ImVec4(0.55f, 0.9f, 0.95f, 1.0f)};
        case spdlog::level::info:
            return {"[Info]", ImVec4(0.76f, 0.86f, 1.0f, 1.0f)};
        case spdlog::level::warn:
            return {"[Warn]", ImVec4(1.0f, 0.82f, 0.35f, 1.0f)};
        case spdlog::level::err:
        case spdlog::level::critical:
            return {"[Error]", ImVec4(1.0f, 0.45f, 0.45f, 1.0f)};
        case spdlog::level::off:
        case spdlog::level::n_levels:
            return {"[Info]", ImVec4(0.78f, 0.78f, 0.78f, 1.0f)};
        }
        return {"[Info]", ImVec4(0.78f, 0.78f, 0.78f, 1.0f)};
    };

    auto ShouldShowLevel = [&](spdlog::level::level_enum level)
    {
        switch (level)
        {
        case spdlog::level::trace:
        case spdlog::level::debug:
            return showDebug;
        case spdlog::level::info:
            return showInfo;
        case spdlog::level::warn:
            return showWarn;
        case spdlog::level::err:
        case spdlog::level::critical:
            return showError;
        case spdlog::level::off:
        case spdlog::level::n_levels:
            return showInfo;
        }
        return true;
    };

    if (ImGui::Button("Clear"))
    {
        clearedLineOffset = lines.size();
        consoleScrollToBottom_ = true;
    }
    ImGui::SameLine();
    consoleFilter.Draw("Filter##ConsoleFilter", 220.0f);
    ImGui::SameLine();
    ImGui::Checkbox("Info", &showInfo);
    ImGui::SameLine();
    ImGui::Checkbox("Warn", &showWarn);
    ImGui::SameLine();
    ImGui::Checkbox("Error", &showError);
    ImGui::SameLine();
    ImGui::Checkbox("Debug", &showDebug);

    std::vector<size_t> visibleLines;
    visibleLines.reserve(lines.size());
    for (size_t i = clearedLineOffset; i < lines.size(); ++i)
    {
        const auto& line = lines[i];
        const std::string payload(line.payload.data(), line.payload.size());
        if (!ShouldShowLevel(line.level))
        {
            continue;
        }
        if (consoleFilter.IsActive() && !consoleFilter.PassFilter(payload.c_str()))
        {
            continue;
        }
        visibleLines.push_back(i);
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
    const ImGuiChildFlags childFlags = bordered ? ImGuiChildFlags_Borders : ImGuiChildFlags_None;
    if (ImGui::BeginChild(childId, size, childFlags))
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visibleLines.size()));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& line = lines[visibleLines[static_cast<size_t>(i)]];
                const auto [prefix, prefixColor] = LevelInfo(line.level);

                const char* payloadStart = line.payload.data();
                const char* payloadEnd = payloadStart + line.payload.size();
                ImGui::PushStyleColor(ImGuiCol_Text, prefixColor);
                ImGui::TextUnformatted(prefix);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, NextUI::Theme::Color(NextUI::Theme::EColor::Text));
                ImGui::TextUnformatted(payloadStart, payloadEnd);
                ImGui::PopStyleColor();
            }
        }

        if (consoleScrollToBottom_ || revision != consoleLogRevision_)
        {
            ImGui::SetScrollHereY(1.0f);
            consoleScrollToBottom_ = false;
            consoleLogRevision_ = revision;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void UserInterface::PreRender()
{
    BeginRendererBackendFrame();
    ImGui_ImplSDL3_NewFrame();
#if ANDROID
    auto& io = ImGui::GetIO();
    io.DisplayFramebufferScale.x *= GAndroidMagicScale;
    io.DisplayFramebufferScale.y *= GAndroidMagicScale;
#endif
    ImGui::NewFrame();
}

void UserInterface::Render(const Statistics& statistics, VulkanGpuTimer* gpuTimer, Assets::Scene* scene,
                           bool suppressStatisticsOverlay)
{
    if (!suppressStatisticsOverlay)
    {
        DrawOverlay(statistics, gpuTimer);
    }
    RenderConsoleOverlay();
}

void UserInterface::PostRender(VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain, uint32_t imageIdx,
                               bool suppressAllUi)
{
    if (suppressAllUi)
    {
        ImGui::EndFrame();
        return;
    }

    if (GetEngine().GetEngineStatus() == NextRenderer::EApplicationStatus::Loading)
        DrawIndicator(GetEngine().GetTotalFrames());

    // aux
    for (auto& req : auxDrawRequest_)
    {
        req();
    }
    auxDrawRequest_.clear();

    ImGui::Render();

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_->Handle();
    renderPassInfo.framebuffer = uiFrameBuffers_[imageIdx].Handle();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = renderPass_->SwapChain().Extent();
    renderPassInfo.clearValueCount = 0;
    renderPassInfo.pClearValues = nullptr;

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    RenderDrawData(ImGui::GetDrawData(), commandBuffer, uiRenderBuffers_[imageIdx], swapChain.Extent(), swapChain.IsHDR(),
                   uiPipeline_);
    vkCmdEndRenderPass(commandBuffer);

    auto& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        PrunePlatformViewportRenderBuffers();
        ImGui::RenderPlatformWindowsDefault(nullptr, this);
    }
}

void UserInterface::HandleEvent(const SDL_Event* event)
{
    if (!event)
    {
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.repeat == 0 &&
        (event->key.key == SDLK_GRAVE || event->key.scancode == SDL_SCANCODE_GRAVE))
    {
        ToggleConsole();
        suppressConsoleToggleTextInput_ = true;
        return;
    }

    if (suppressConsoleToggleTextInput_ && event->type == SDL_EVENT_TEXT_INPUT)
    {
        const bool isConsoleToggleText =
            std::strcmp(event->text.text, "`") == 0 || std::strcmp(event->text.text, "~") == 0;
        suppressConsoleToggleTextInput_ = false;
        if (isConsoleToggleText)
        {
            return;
        }
    }

    ImGui_ImplSDL3_ProcessEvent(event);
}

bool UserInterface::WantsToCaptureKeyboard() const { return ImGui::GetIO().WantCaptureKeyboard; }

bool UserInterface::WantsToCaptureMouse() const { return ImGui::GetIO().WantCaptureMouse; }

void UserInterface::ToggleConsole()
{
    showConsole_ = !showConsole_;
    requestConsoleFocus_ = showConsole_;
}

void UserInterface::RenderConsoleOverlay()
{
    DrawConsoleWindow();
}

void UserInterface::DrawOverlay(const Statistics& statistics, VulkanGpuTimer* gpuTimer)
{
    if (!Settings().ShowOverlay)
    {
        return;
    }

    if (overlaySampleStrideCounter_ == 0)
    {
        frameRateSamples_[overlaySampleCursor_] = statistics.FrameRate;
        frameTimeSamples_[overlaySampleCursor_] = statistics.FrameTime;
        overlaySampleCursor_ = (overlaySampleCursor_ + 1) % kOverlaySparklineSampleCount;
        overlaySampleFilled_ = std::min(overlaySampleFilled_ + 1, kOverlaySparklineSampleCount);
    }
    overlaySampleStrideCounter_ = (overlaySampleStrideCounter_ + 1) % kOverlaySparklineSampleStride;

    const auto& io = ImGui::GetIO();
    constexpr float distance = 12.0f;
    constexpr float panelWidth = 380.0f;
    const ImVec2 pos = ImVec2(io.DisplaySize.x - distance - panelWidth, distance + 44.0f);
    const float panelHeight = std::max(420.0f, io.DisplaySize.y - pos.y - 42.0f);

    if (!NextUI::Theme::BeginFloatingPanel(
            "##ProfilerPanel", ICON_FA_CHART_LINE, "Profiler", &Settings().ShowOverlay,
            pos, ImVec2(panelWidth, panelHeight)))
    {
        return;
    }

    ImGui::BeginChild("##ProfilerBody", ImVec2(0, 0), false, ImGuiWindowFlags_NoBackground);

    constexpr float cardHorizontalInset = 4.0f;
    auto BeginCard = [&](const char* id, float height, ImGuiWindowFlags extraFlags = 0)
    {
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        const float cardWidth = std::max(0.0f, ImGui::GetContentRegionAvail().x - cardHorizontalInset * 2.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cardHorizontalInset);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, NextUI::Theme::Color(NextUI::Theme::EColor::SurfaceElevated, 0.38f));
        ImGui::PushStyleColor(ImGuiCol_Border, NextUI::Theme::Color(NextUI::Theme::EColor::Border, 0.84f));
        ImGui::BeginChild(id, ImVec2(cardWidth, height), true, extraFlags);
    };

    auto EndCard = [&]()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    };

    auto BuildOrdered = [&](const std::array<float, kOverlaySparklineSampleCount>& src,
                            std::array<float, kOverlaySparklineSampleCount>& dst,
                            int& outCount)
    {
        outCount = overlaySampleFilled_;
        if (overlaySampleFilled_ < kOverlaySparklineSampleCount)
        {
            for (int i = 0; i < outCount; ++i)
            {
                dst[i] = src[i];
            }
        }
        else
        {
            for (int i = 0; i < kOverlaySparklineSampleCount; ++i)
            {
                dst[i] = src[(overlaySampleCursor_ + i) % kOverlaySparklineSampleCount];
            }
        }
    };

    std::array<float, kOverlaySparklineSampleCount> orderedFps{};
    std::array<float, kOverlaySparklineSampleCount> orderedFt{};
    int orderedCount = 0;
    BuildOrdered(frameRateSamples_, orderedFps, orderedCount);
    BuildOrdered(frameTimeSamples_, orderedFt, orderedCount);

    const ImVec4 colHeader = NextUI::Theme::Color(NextUI::Theme::EColor::Blue);
    const ImVec4 colLabel = NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted);
    const ImVec4 colVal = NextUI::Theme::Color(NextUI::Theme::EColor::Text);
    const ImVec4 colGood = NextUI::Theme::Color(NextUI::Theme::EColor::Success);
    const ImVec4 colWarn = NextUI::Theme::Color(NextUI::Theme::EColor::Warning);
    const ImVec4 colBad = NextUI::Theme::Color(NextUI::Theme::EColor::Danger);

    auto LabelVal = [&](const char* label, const char* fmt, auto... args)
    {
        ImGui::TextColored(colLabel, "%s", label);
        ImGui::SameLine(132.0f);
        ImGui::TextColored(colVal, fmt, args...);
    };

    {
        const Vulkan::Device& device = NextEngine::GetInstance()->GetRenderer().Device();
        const VkPhysicalDeviceProperties deviceProperties = device.DeviceProperties();
        const std::string driverName = GetPhysicalDeviceDriverName(device.PhysicalDevice());

        const ImVec4 fpsColor = statistics.FrameRate > 55.0f ? colGood
            : (statistics.FrameRate > 30.0f ? colWarn : colBad);
        const std::string fpsText = fmt::format("{:.0f}  FPS", statistics.FrameRate);
        const std::string ftText = fmt::format("{:.2f}  ms", statistics.FrameTime);

        BeginCard("##ProfilerDeviceCard", 180.0f);
        if (ImGui::BeginTable("##ProfilerDeviceHeader", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableNextColumn();
            ImGui::TextColored(colHeader, "Device");
            ImGui::TableNextColumn();
            ImGui::TextColored(colLabel, "Resolution");
            ImGui::SameLine(0.0f, 8.0f);
            ImGui::TextColored(colVal, "%ux%u", statistics.FramebufferSize.width,
                               statistics.FramebufferSize.height);
            ImGui::EndTable();
        }
        ImGui::TextColored(colVal, "%s", deviceProperties.deviceName);
        if (!driverName.empty())
        {
            const float driverFontSize = ImGui::GetFontSize() * 0.84f;
            const ImVec4 driverColor = NextUI::Theme::Color(NextUI::Theme::EColor::TextMuted, 0.72f);
            const ImVec2 driverPos = ImGui::GetCursorScreenPos();
            ImFont* font = ImGui::GetFont();
            const ImVec2 driverSize = font->CalcTextSizeA(driverFontSize, FLT_MAX, 0.0f, driverName.c_str());
            ImGui::GetWindowDrawList()->AddText(font, driverFontSize, driverPos,
                                                ImGui::GetColorU32(driverColor), driverName.c_str());
            ImGui::Dummy(ImVec2(driverSize.x, driverSize.y + 2.0f));
        }

        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        if (ImGui::BeginTable("##ProfilerSparklineTable", 2, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableSetupColumn("Frame Rate", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Frame Time", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(colHeader, "Frame Rate");
            ImGui::TextColored(fpsColor, "%s", fpsText.c_str());
            NextUI::Theme::Sparkline(orderedFps.data(), orderedCount,
                                     ImVec2(ImGui::GetContentRegionAvail().x, 26.0f), colGood);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(colHeader, "Frame Time");
            ImGui::TextColored(colVal, "%s", ftText.c_str());
            NextUI::Theme::Sparkline(orderedFt.data(), orderedCount,
                                     ImVec2(ImGui::GetContentRegionAvail().x, 26.0f),
                                     NextUI::Theme::Color(NextUI::Theme::EColor::Blue));
            ImGui::EndTable();
        }
        EndCard();
    }

    auto& gpuDrivenStat = NextEngine::GetInstance()->GetScene().GetGpuDrivenStat();
    const auto& shadowGpuDrivenStats = NextEngine::GetInstance()->GetScene().GetShadowGpuDrivenStats();
    const uint32_t instanceCount = gpuDrivenStat.ProcessedCount > gpuDrivenStat.CulledCount
        ? gpuDrivenStat.ProcessedCount - gpuDrivenStat.CulledCount
        : 0;
    const uint32_t triangleCount = gpuDrivenStat.TriangleCount > gpuDrivenStat.CulledTriangleCount
        ? gpuDrivenStat.TriangleCount - gpuDrivenStat.CulledTriangleCount
        : 0;
    const uint32_t mainTasks = Tasks::TaskCoordinator::GetInstance()->GetMainTaskCount();
    const uint32_t lowTasks = Tasks::TaskCoordinator::GetInstance()->GetParralledTaskCount();
    const uint32_t completeTasks = Tasks::TaskCoordinator::GetInstance()->GetComleteTaskQueueCount();

    auto FormatVisibleOverTotal = [](uint32_t visibleCount, uint32_t totalCount)
    {
        return fmt::format("{} / {}",
                           Utilities::metricFormatter(static_cast<double>(visibleCount), ""),
                           Utilities::metricFormatter(static_cast<double>(totalCount), ""));
    };

    BeginCard("##ProfilerSceneStatsCard", 308.0f);
    ImGui::TextColored(colHeader, "Scene Stats");
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    std::vector<std::pair<std::string, std::string>> sceneStats = {
        {"Nodes", Utilities::metricFormatter(static_cast<double>(statistics.NodeCount), "")},
        {"Instances", Utilities::metricFormatter(static_cast<double>(statistics.InstanceCount), "")},
        {"Textures", std::to_string(statistics.TextureCount)},
        {"Draws", FormatVisibleOverTotal(instanceCount, gpuDrivenStat.ProcessedCount)},
        {"Triangles", FormatVisibleOverTotal(triangleCount, gpuDrivenStat.TriangleCount)},
        {"Tasks", fmt::format("{} / {} / {}", mainTasks, lowTasks, completeTasks)},
    };
    if ((sceneStats.size() & 1u) != 0u)
    {
        sceneStats.emplace_back("", "");
    }
    if (ImGui::BeginTable("##SceneStatsTable", 2, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableSetupColumn("Left", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch);
        auto DrawSceneStat = [&](const std::pair<std::string, std::string>& stat)
        {
            if (!stat.first.empty())
            {
                ImGui::TextColored(colLabel, "%s", stat.first.c_str());
                ImGui::TextColored(colVal, "%s", stat.second.c_str());
            }
        };
        for (size_t statIndex = 0; statIndex < sceneStats.size(); statIndex += 2)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            DrawSceneStat(sceneStats[statIndex]);
            ImGui::TableSetColumnIndex(1);
            DrawSceneStat(sceneStats[statIndex + 1]);
        }
        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextColored(colHeader, "Shadow Cascades");
    if (ImGui::BeginTable("##SceneShadowCascadeStatsTable", 3,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                              ImGuiTableFlags_SizingFixedFit))
    {
        ImGui::TableSetupColumn("Cascade", ImGuiTableColumnFlags_WidthFixed, 56.0f);
        ImGui::TableSetupColumn("Draws", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableSetupColumn("Tri", ImGuiTableColumnFlags_WidthFixed, 92.0f);
        ImGui::TableHeadersRow();

        for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
        {
            const auto& stat = shadowGpuDrivenStats[cascade];
            const uint32_t shadowDrawCount = stat.ProcessedCount > stat.CulledCount
                ? stat.ProcessedCount - stat.CulledCount
                : 0;
            const uint32_t shadowTriangleCount = stat.TriangleCount > stat.CulledTriangleCount
                ? stat.TriangleCount - stat.CulledTriangleCount
                : 0;

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(colLabel, "C%u", cascade);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(colVal, "%s",
                               FormatVisibleOverTotal(shadowDrawCount, stat.ProcessedCount).c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextColored(colVal, "%s",
                               FormatVisibleOverTotal(shadowTriangleCount, stat.TriangleCount).c_str());
        }
        ImGui::EndTable();
    }
    EndCard();

    struct TimingRow
    {
        std::string name;
        int depth = 0;
        float average = 0.0f;
        float minimum = 0.0f;
        float maximum = 0.0f;
        uint32_t displayOrder = 0;
        bool active = true;
    };

    constexpr double timingHistoryWindowSeconds = 2.0;
    constexpr double timingStaleSeconds = 3.0;
    const double now = ImGui::GetTime();

    auto BuildTimingRows = [&](const std::vector<VulkanGpuTimer::TimerStat>& times,
                               std::unordered_map<std::string, TimingHistory>& historyMap)
    {
        uint32_t currentDisplayOrder = 0;
        for (const auto& time : times)
        {
            const std::string& historyKey = time.stableKey;
            auto historyIter = historyMap.try_emplace(historyKey).first;
            auto& history = historyIter->second;

            // Keep stale timers in their previous slot so transient timers do not
            // cause the rest of the table to jump every frame. Existing rows only
            // move when the current traversal order would otherwise place them
            // above a timer we have already emitted this frame.
            if (history.displayOrder < currentDisplayOrder)
            {
                history.displayOrder = currentDisplayOrder;
            }
            currentDisplayOrder = history.displayOrder + 1;
            history.displayName = time.name;
            history.depth = time.depth;
            history.lastSeenTime = now;
            history.samples.push_back({now, time.milliseconds});

            while (!history.samples.empty() &&
                   now - history.samples.front().sampleTime > timingHistoryWindowSeconds)
            {
                history.samples.pop_front();
            }

            float sum = 0.0f;
            float minimum = 1000000.0f;
            float maximum = 0.0f;
            for (const auto& sample : history.samples)
            {
                sum += sample.milliseconds;
                minimum = std::min(minimum, sample.milliseconds);
                maximum = std::max(maximum, sample.milliseconds);
            }

            history.average = history.samples.empty() ? time.milliseconds : sum / static_cast<float>(history.samples.size());
            history.minimum = minimum;
            history.maximum = maximum;
        }

        for (auto iter = historyMap.begin(); iter != historyMap.end();)
        {
            auto& history = iter->second;
            while (!history.samples.empty() &&
                   now - history.samples.front().sampleTime > timingHistoryWindowSeconds)
            {
                history.samples.pop_front();
            }

            if (now - iter->second.lastSeenTime > timingStaleSeconds)
            {
                iter = historyMap.erase(iter);
            }
            else
            {
                ++iter;
            }
        }

        std::vector<TimingRow> timingRows;
        timingRows.reserve(historyMap.size());
        for (const auto& [key, history] : historyMap)
        {
            timingRows.push_back({history.displayName,
                                  history.depth,
                                  history.average,
                                  history.minimum,
                                  history.maximum,
                                  history.displayOrder,
                                  now - history.lastSeenTime <= 0.1});
        }
        std::sort(timingRows.begin(), timingRows.end(), [](const TimingRow& lhs, const TimingRow& rhs)
        {
            if (lhs.displayOrder != rhs.displayOrder)
            {
                return lhs.displayOrder < rhs.displayOrder;
            }
            if (lhs.active != rhs.active)
            {
                return lhs.active;
            }
            return lhs.name < rhs.name;
        });
        return timingRows;
    };

    auto DrawTimingSection = [&](const char* label, const char* tableId, const std::vector<TimingRow>& timingRows)
    {
        float totalTime = 0.0f;
        for (const auto& row : timingRows)
        {
            if (row.depth == 0)
            {
                totalTime += row.average;
            }
        }

        ImGui::TextColored(colHeader, "%s (avg %.2fms / %.1fs)", label, totalTime, timingHistoryWindowSeconds);

        auto TimingBarColor = [&](float milliseconds)
        {
            if (milliseconds < 1.0f)
            {
                return colGood;
            }
            if (milliseconds < 4.0f)
            {
                return colWarn;
            }
            return colBad;
        };

        if (ImGui::BeginTable(tableId, 5,
                              ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                  ImGuiTableFlags_SizingFixedFit))
        {
            ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableHeadersRow();

            for (const auto& row : timingRows)
            {
                const float ratio = totalTime > 0.001f ? row.average / totalTime : 0.0f;
                const ImVec4 rowColor = row.depth == 0 ? colVal : colLabel;

                ImGui::TableNextRow();
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, row.active ? 1.0f : 0.45f);
                ImGui::TableNextColumn();
                ImGui::Indent(static_cast<float>(row.depth) * 12.0f);
                ImGui::TextColored(rowColor, "%s", row.name.c_str());
                ImGui::Unindent(static_cast<float>(row.depth) * 12.0f);

                ImGui::TableNextColumn();
                ImGui::TextColored(rowColor, "%.2f", row.average);
                ImGui::TableNextColumn();
                ImGui::TextColored(colLabel, "%.2f", row.minimum);
                ImGui::TableNextColumn();
                ImGui::TextColored(colLabel, "%.2f", row.maximum);
                ImGui::TableNextColumn();
                NextUI::Theme::DrawProgressBar(std::min(ratio, 1.0f),
                                                  TimingBarColor(row.average),
                                                  ImVec2(70.0f, ImGui::GetTextLineHeight()));
                ImGui::PopStyleVar();
            }
            ImGui::EndTable();
        }
    };

    const float timingCardHeight = std::max(180.0f, ImGui::GetContentRegionAvail().y - 42.0f);
    BeginCard("##ProfilerTimingCard", timingCardHeight, ImGuiWindowFlags_HorizontalScrollbar);
    if (gpuTimer)
    {
        const auto gpuTimingRows = BuildTimingRows(gpuTimer->FetchAllTimes(4), gpuTimeHistory_);
        DrawTimingSection("Pass Timing", "##GpuTimeTable", gpuTimingRows);

        const auto cpuTimingRows = BuildTimingRows(gpuTimer->FetchAllCpuTimes(5), cpuTimeHistory_);
        if (!cpuTimingRows.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            NextUI::Theme::DrawThinSeparator(0.55f);
            DrawTimingSection("CPU Time", "##CpuTimeTable", cpuTimingRows);
        }
    }
    else
    {
        ImGui::TextColored(colLabel, "Timing data is unavailable.");
    }
    EndCard();

    ImGui::EndChild();
    NextUI::Theme::EndFloatingPanel();
}

void UserInterface::DrawIndicator(uint32_t frameCount)
{
    frameCount /= 60;
    ImGui::OpenPopup("Loading");
    // Always center this window when appearing
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(200, 40));

    if (ImGui::BeginPopupModal("Loading", NULL,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize))
    {
        ImGui::Text("Loading%s",
                    frameCount % 4 == 0       ? ""
                        : frameCount % 4 == 1 ? "."
                        : frameCount % 4 == 2 ? ".."
                                              : "...");
        ImGui::EndPopup();
    }
}

void UserInterface::DrawConsoleWindow()
{
    if (!showConsole_)
    {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport == nullptr)
    {
        return;
    }

    constexpr float kBottomBarReservedHeight = 34.0f;
    const ImVec2 windowPos = viewport->Pos;
    const ImVec2 windowSize(viewport->Size.x, std::max(140.0f, viewport->Size.y - kBottomBarReservedHeight));

    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(windowSize, ImGuiCond_Always);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowBgAlpha(0.7f);

    const auto flags = ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("Console", &showConsole_, flags))
    {
        const size_t kConsoleMatchLimit = 8;
        RefreshConsoleMatches(kConsoleMatchLimit);

        float hintHeight = 0.0f;
        if (!consoleMatches_.empty())
        {
            hintHeight = ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(consoleMatches_.size()) + 1.0f);
        }

        float inputHeight = ImGui::GetFrameHeightWithSpacing();
        float outputHeight = ImGui::GetContentRegionAvail().y - inputHeight - hintHeight;
        outputHeight = std::max(outputHeight, ImGui::GetFontSize() * 5.0f);

        DrawConsoleLogOutputInternal("ConsoleOutput", ImVec2(0, outputHeight), true);

        if (!consoleMatches_.empty())
        {
            ImGui::BeginChild("ConsoleMatches", ImVec2(0, hintHeight), false, ImGuiWindowFlags_NoScrollbar);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
            ImGui::Text("Matches (%zu/%zu):", consoleMatches_.size(), kConsoleMatchLimit);
            ImGui::PopStyleColor();
            for (const auto& name : consoleMatches_)
            {
                ImGui::Text("%s", name.c_str());
            }
            ImGui::EndChild();
        }

        if (requestConsoleFocus_)
        {
            ImGui::SetKeyboardFocusHere();
            requestConsoleFocus_ = false;
        }

        ImGui::PushItemWidth(-1);
        DrawConsoleCommandInput("##ConsoleInput", "", -1.0f, true, false, nullptr, false);
        ImGui::PopItemWidth();
    }
    ImGui::End();
}

}
