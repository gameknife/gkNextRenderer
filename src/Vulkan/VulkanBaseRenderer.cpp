#include "VulkanBaseRenderer.hpp"
#include "Buffer.hpp"
#include "CommandPool.hpp"
#include "CommandBuffers.hpp"
#include "DebugUtilsMessenger.hpp"
#include "DepthBuffer.hpp"
#include "Device.hpp"
#include "Fence.hpp"
#include "FrameBuffer.hpp"
#include "GraphicsPipeline.hpp"
#include "Instance.hpp"
#include "PipelineLayout.hpp"
#include "RenderPass.hpp"
#include "Semaphore.hpp"
#include "Surface.hpp"
#include "SwapChain.hpp"
#include "Window.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Assets/Texture.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Console.hpp"
#include <array>
#include <fmt/format.h>

#include "Enumerate.hpp"
#include "ImageMemoryBarrier.hpp"
#include "Options.hpp"
#include "RenderImage.hpp"
#include "SingleTimeCommands.hpp"
#include "Strings.hpp"
#include "Version.hpp"
#include "HybridDeferred/HybridDeferredRenderer.hpp"
#include "LegacyDeferred/LegacyDeferredRenderer.hpp"
#include "ModernDeferred/ModernDeferredRenderer.hpp"
#include "RayQuery/RayQueryRenderer.hpp"
#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"

namespace
{
    void PrintVulkanSdkInformation()
    {
        fmt::print("Vulkan SDK Header Version: {}\n\n", VK_HEADER_VERSION);
    }

    void PrintVulkanInstanceInformation(const Vulkan::VulkanBaseRenderer& application, const bool benchmark)
    {
        if (benchmark)
        {
            return;
        }

        puts("Vulkan Instance Extensions:");

        for (const auto& extension : application.Extensions())
        {
            fmt::print("- {} ({})\n", extension.extensionName, to_string(Vulkan::Version(extension.specVersion)));
        }

        puts("");
    }

    void PrintVulkanLayersInformation(const Vulkan::VulkanBaseRenderer& application, const bool benchmark)
    {
        if (benchmark)
        {
            return;
        }

        puts("Vulkan Instance Layers:");

        for (const auto& layer : application.Layers())
        {
            fmt::print("- {} ({}) : {}\n", layer.layerName, to_string(Vulkan::Version(layer.specVersion)), layer.description);
        }

        puts("");
    }

    void PrintVulkanDevices(const Vulkan::VulkanBaseRenderer& application)
    {
        puts("Vulkan Devices:");

        for (const auto& device : application.PhysicalDevices())
        {
            VkPhysicalDeviceDriverProperties driverProp{};
            driverProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;

            VkPhysicalDeviceProperties2 deviceProp{};
            deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            deviceProp.pNext = &driverProp;
            vkGetPhysicalDeviceProperties2(device, &deviceProp);
            VkPhysicalDeviceFeatures features;
            vkGetPhysicalDeviceFeatures(device, &features);

            const auto& prop = deviceProp.properties;

            const Vulkan::Version vulkanVersion(prop.apiVersion);
            const Vulkan::Version driverVersion(prop.driverVersion, prop.vendorID);

            fmt::print("- [{}] {} '{}' ({}: vulkan {} driver {} {} - {})\n",
                       prop.deviceID, Vulkan::Strings::VendorId(prop.vendorID), prop.deviceName, Vulkan::Strings::DeviceType(prop.deviceType),
                       to_string(vulkanVersion), driverProp.driverName, driverProp.driverInfo, to_string(driverVersion));

            const auto extensions = Vulkan::GetEnumerateVector(device, static_cast<const char*>(nullptr), vkEnumerateDeviceExtensionProperties);
            const auto hasRayTracing = std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& extension)
            {
                return strcmp(extension.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0;
            });
        }

        puts("");
    }

    bool SupportRayQuery(const Vulkan::VulkanBaseRenderer& application)
    {
        bool SupportRayQuery = false;
        for (const auto& device : application.PhysicalDevices())
        {
            const auto extensions = Vulkan::GetEnumerateVector(device, static_cast<const char*>(nullptr), vkEnumerateDeviceExtensionProperties);
            const auto hasRayTracing = std::any_of(extensions.begin(), extensions.end(), [](const VkExtensionProperties& extension)
            {
                return strcmp(extension.extensionName, VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0;
            });

            SupportRayQuery = SupportRayQuery | hasRayTracing;
        }
        return SupportRayQuery;
    }

    void PrintVulkanSwapChainInformation(const Vulkan::VulkanBaseRenderer& application)
    {
        const auto& swapChain = application.SwapChain();

        fmt::print("Swap Chain:\n- image count: {}\n- present mode: {}\n\n", swapChain.Images().size(), static_cast<int>(swapChain.PresentMode()));
    }

    void SetVulkanDevice(Vulkan::VulkanBaseRenderer& application, uint32_t gpuIdx)
    {
        const auto& physicalDevices = application.PhysicalDevices();
        VkPhysicalDevice pDevice = physicalDevices[gpuIdx <= physicalDevices.size() ? gpuIdx : 0];
        VkPhysicalDeviceProperties2 deviceProp{};
        deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(pDevice, &deviceProp);

        fmt::print("Setting Device [{}]\n", deviceProp.properties.deviceName);
        application.SetPhysicalDevice(pDevice);

        puts("");
    }
}

namespace Vulkan
{
    VulkanBaseRenderer::VulkanBaseRenderer(Vulkan::Window* window, const VkPresentModeKHR presentMode, const bool enableValidationLayers) :
        presentMode_(presentMode)
    {
        const auto validationLayers = enableValidationLayers
                                          ? std::vector<const char*>{"VK_LAYER_KHRONOS_validation"}
                                          : std::vector<const char*>{};

        window_ = window;
        instance_.reset(new Instance(*window_, validationLayers, VK_API_VERSION_1_2));
        debugUtilsMessenger_.reset(enableValidationLayers ? new DebugUtilsMessenger(*instance_, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) : nullptr);
        surface_.reset(new Surface(*instance_));
        supportDenoiser_ = false;
        forceSDR_ = GOption->ForceSDR;

        uptime = std::chrono::high_resolution_clock::now().time_since_epoch().count();

        supportRayTracing_ = SupportRayQuery(*this);
    }

    VulkanGpuTimer::VulkanGpuTimer(VkDevice device, uint32_t totalCount, const VkPhysicalDeviceProperties& prop)
    {
        device_ = device;
        time_stamps.resize(totalCount);
        timeStampPeriod_ = prop.limits.timestampPeriod;
        // Create the query pool object used to get the GPU time tamps
        VkQueryPoolCreateInfo query_pool_info{};
        query_pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        // We need to specify the query type for this pool, which in our case is for time stamps
        query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        // Set the no. of queries in this pool
        query_pool_info.queryCount = static_cast<uint32_t>(time_stamps.size());
        Check(vkCreateQueryPool(device, &query_pool_info, nullptr, &query_pool_timestamps), "create timestamp pool");
    }

    VulkanGpuTimer::~VulkanGpuTimer()
    {
        vkDestroyQueryPool(device_, query_pool_timestamps, nullptr);
    }

    VulkanBaseRenderer::~VulkanBaseRenderer()
    {
        VulkanBaseRenderer::DeleteSwapChain();

        rtEditorViewport_.reset();
        gpuTimer_.reset();
        globalTexturePool_.reset();
        commandPool_.reset();
        commandPool2_.reset();
        device_.reset();
        surface_.reset();
        debugUtilsMessenger_.reset();
        instance_.reset();
        window_ = nullptr;
    }

    const std::vector<VkExtensionProperties>& VulkanBaseRenderer::Extensions() const
    {
        return instance_->Extensions();
    }

    const std::vector<VkLayerProperties>& VulkanBaseRenderer::Layers() const
    {
        return instance_->Layers();
    }

    const std::vector<VkPhysicalDevice>& VulkanBaseRenderer::PhysicalDevices() const
    {
        return instance_->PhysicalDevices();
    }

    void VulkanBaseRenderer::SetPhysicalDevice(VkPhysicalDevice physicalDevice)
    {
        if (device_)
        {
            Throw(std::logic_error("physical device has already been set"));
        }

        std::vector<const char*> requiredExtensions =
        {
            // VK_KHR_swapchain
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if __APPLE__
		"VK_KHR_portability_subset",
#endif
        };


        VkPhysicalDeviceFeatures deviceFeatures = {};

        deviceFeatures.multiDrawIndirect = true;
        deviceFeatures.drawIndirectFirstInstance = true;

        SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, nullptr);

        // Global Texture Pool Creation Here
        globalTexturePool_.reset(new Assets::GlobalTexturePool(*device_, *commandPool2_, *commandPool_));

        OnDeviceSet();

        // Create swap chain and command buffers.
        CreateSwapChain();

        window_->Show();

        uptime = std::chrono::high_resolution_clock::now().time_since_epoch().count() - uptime;
        fmt::print("\n{} renderer initialized in {:.2f}ms{}\n", CONSOLE_GREEN_COLOR, uptime * 1e-6f, CONSOLE_DEFAULT_COLOR);
    }

    void VulkanBaseRenderer::Start()
    {
        // setup vulkan

        PrintVulkanSdkInformation();
        //PrintVulkanInstanceInformation(*GApplication, options.Benchmark);
        //PrintVulkanLayersInformation(*GApplication, options.Benchmark);
        PrintVulkanDevices(*this);
        SetVulkanDevice(*this, GOption->GpuIdx);
        PrintVulkanSwapChainInformation(*this);

        currentFrame_ = 0;
    }

    void VulkanBaseRenderer::End()
    {
        device_->WaitIdle();
        gpuTimer_.reset();
    }

    Assets::Scene& VulkanBaseRenderer::GetScene()
    {
        return *scene_.lock();
    }

    void VulkanBaseRenderer::SetScene(std::shared_ptr<Assets::Scene> scene)
    {
        scene_ = scene;
    }

    Assets::UniformBufferObject VulkanBaseRenderer::GetUniformBufferObject(const VkOffset2D offset, const VkExtent2D extent) const
    {
        if (DelegateGetUniformBufferObject)
        {
            return DelegateGetUniformBufferObject(offset, extent);
        }
        return {};
    }

    void VulkanBaseRenderer::SetPhysicalDeviceImpl(
        VkPhysicalDevice physicalDevice,
        std::vector<const char*>& requiredExtensions,
        VkPhysicalDeviceFeatures& deviceFeatures,
        void* nextDeviceFeatures)
    {
        deviceFeatures.fillModeNonSolid = true;
        deviceFeatures.samplerAnisotropy = true;

        // Required extensions. windows only
#if WIN32
        requiredExtensions.insert(requiredExtensions.end(),
                                  {
                                      // VK_KHR_SHADER_CLOCK is required for heatmap
                                      VK_KHR_SHADER_CLOCK_EXTENSION_NAME
                                  });

        // Opt-in into mandatory device features.
        VkPhysicalDeviceShaderClockFeaturesKHR shaderClockFeatures = {};
        shaderClockFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
        shaderClockFeatures.pNext = nextDeviceFeatures;
        shaderClockFeatures.shaderSubgroupClock = true;

        deviceFeatures.shaderInt64 = true;
#endif

        // support bindless material
        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
        indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
#if WIN32
        indexingFeatures.pNext = &shaderClockFeatures;
#else
	indexingFeatures.pNext = nextDeviceFeatures;
#endif
        indexingFeatures.runtimeDescriptorArray = true;
        indexingFeatures.shaderSampledImageArrayNonUniformIndexing = true;
        indexingFeatures.descriptorBindingPartiallyBound = true;
        indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = true;
        indexingFeatures.descriptorBindingVariableDescriptorCount = true;


        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
        bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddressFeatures.pNext = &indexingFeatures;
        bufferDeviceAddressFeatures.bufferDeviceAddress = true;

        VkPhysicalDeviceHostQueryResetFeaturesEXT hostQueryResetFeatures = {};
        hostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
        hostQueryResetFeatures.pNext = &bufferDeviceAddressFeatures;
        hostQueryResetFeatures.hostQueryReset = true;

        device_.reset(new class Device(physicalDevice, *surface_, requiredExtensions, deviceFeatures, &hostQueryResetFeatures));
        commandPool_.reset(new class CommandPool(*device_, device_->GraphicsFamilyIndex(), 0, true));
        commandPool2_.reset(new class CommandPool(*device_, device_->TransferFamilyIndex(), 1, true));
        gpuTimer_.reset(new VulkanGpuTimer(device_->Handle(), 10 * 2, device_->DeviceProperties()));
    }

    void VulkanBaseRenderer::OnDeviceSet()
    {
        for (auto& logicRenderer : logicRenderers_)
        {
            logicRenderer.second->OnDeviceSet();
        }

        if (DelegateOnDeviceSet)
        {
            DelegateOnDeviceSet();
        }
    }

    void VulkanBaseRenderer::CreateSwapChain()
    {
        // Wait until the window is visible.
        while (window_->IsMinimized())
        {
            window_->WaitForEvents();
        }

        swapChain_.reset(new class SwapChain(*device_, presentMode_, forceSDR_));
        depthBuffer_.reset(new class DepthBuffer(*commandPool_, swapChain_->Extent()));

        for (size_t i = 0; i != swapChain_->ImageViews().size(); ++i)
        {
            imageAvailableSemaphores_.emplace_back(*device_);
            renderFinishedSemaphores_.emplace_back(*device_);
            inFlightFences_.emplace_back(*device_, true);
            uniformBuffers_.emplace_back(*device_);
        }

        graphicsPipeline_.reset(new class GraphicsPipeline(*swapChain_, *depthBuffer_, uniformBuffers_, GetScene(), true));
        bufferClearPipeline_.reset(new class PipelineCommon::BufferClearPipeline(*swapChain_));

        for (const auto& imageView : swapChain_->ImageViews())
        {
            swapChainFramebuffers_.emplace_back(*imageView, graphicsPipeline_->SwapRenderPass());
        }

        commandBuffers_.reset(new CommandBuffers(*commandPool_, static_cast<uint32_t>(swapChainFramebuffers_.size())));

        fence = nullptr;

        screenShotImage_.reset(new Image(*device_, swapChain_->Extent(), 1, swapChain_->Format(), VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT));
        screenShotImageMemory_.reset(new DeviceMemory(screenShotImage_->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));

        rtEditorViewport_.reset(new RenderImage(*device_, {1280, 720}, swapChain_->Format(), VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT));

        for (auto& logicRenderer : logicRenderers_)
        {
            logicRenderer.second->CreateSwapChain();
        }

        if (DelegateCreateSwapChain)
        {
            DelegateCreateSwapChain();
        }
    }

    void VulkanBaseRenderer::DeleteSwapChain()
    {
        for (auto& logicRenderer : logicRenderers_)
        {
            logicRenderer.second->DeleteSwapChain();
        }

        if (DelegateDeleteSwapChain)
        {
            DelegateDeleteSwapChain();
        }

        screenShotImageMemory_.reset();
        screenShotImage_.reset();
        commandBuffers_.reset();
        swapChainFramebuffers_.clear();
        graphicsPipeline_.reset();
        bufferClearPipeline_.reset();
        uniformBuffers_.clear();
        inFlightFences_.clear();
        renderFinishedSemaphores_.clear();
        imageAvailableSemaphores_.clear();
        depthBuffer_.reset();
        swapChain_.reset();
        fence = nullptr;
    }

    void VulkanBaseRenderer::CaptureScreenShot()
    {
        SingleTimeCommands::Submit(CommandPool(), [&](VkCommandBuffer commandBuffer)
        {
            const auto& image = swapChain_->Images()[currentImageIndex_];

            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = 1;
            subresourceRange.baseArrayLayer = 0;
            subresourceRange.layerCount = 1;

            ImageMemoryBarrier::Insert(commandBuffer, image, subresourceRange,
                                       0, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            ImageMemoryBarrier::Insert(commandBuffer, screenShotImage_->Handle(), subresourceRange, 0,
                                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            // Copy output image into swap-chain image.
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {SwapChain().Extent().width, SwapChain().Extent().height, 1};

            vkCmdCopyImage(commandBuffer,
                           image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           screenShotImage_->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[currentImageIndex_], subresourceRange,
                                       VK_ACCESS_TRANSFER_READ_BIT,
                                       0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        });
    }

    void VulkanBaseRenderer::CaptureEditorViewport(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const auto& image = swapChain_->Images()[imageIndex];

        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        ImageMemoryBarrier::Insert(commandBuffer, image, subresourceRange,
                                   0, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        ImageMemoryBarrier::Insert(commandBuffer, rtEditorViewport_->GetImage().Handle(), subresourceRange, 0,
                                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        // Copy output image into swap-chain image.
        VkImageCopy copyRegion;
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.srcOffset = {0, 0, 0};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstOffset = {0, 0, 0};
        copyRegion.extent = {rtEditorViewport_->GetImage().Extent().width, rtEditorViewport_->GetImage().Extent().height, 1};

        vkCmdCopyImage(commandBuffer,
                       image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       rtEditorViewport_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &copyRegion);

        ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                                   VK_ACCESS_TRANSFER_READ_BIT,
                                   0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        ImageMemoryBarrier::Insert(commandBuffer, rtEditorViewport_->GetImage().Handle(), subresourceRange, VK_ACCESS_TRANSFER_WRITE_BIT,
                                   VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    void VulkanBaseRenderer::ClearViewport(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        {
            SCOPED_GPU_TIMER("clear pass");

            VkImageSubresourceRange subresourceRange = {};
            subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresourceRange.baseMipLevel = 0;
            subresourceRange.levelCount = 1;
            subresourceRange.baseArrayLayer = 0;
            subresourceRange.layerCount = 1;

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
                                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_GENERAL);

            VkDescriptorSet DescriptorSets[] = {bufferClearPipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, bufferClearPipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    bufferClearPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
    }

    void VulkanBaseRenderer::DrawFrame()
    {
        {
            PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::DrawFrame", PERFORMANCEAPI_MAKE_COLOR(200, 255, 200));
            SCOPED_CPU_TIMER("draw-frame");
            const auto noTimeout = std::numeric_limits<uint64_t>::max();

            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::Prepare", PERFORMANCEAPI_MAKE_COLOR(255, 255, 200));
                SCOPED_CPU_TIMER("cpu-prepare");
                BeforeNextFrame();
            }

            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::QueryWait", PERFORMANCEAPI_MAKE_COLOR(255, 255, 200));
                SCOPED_CPU_TIMER("query-wait");
                gpuTimer_->FrameEnd((*commandBuffers_)[currentImageIndex_]);
            }

            // next frame synchronization objects
            const auto imageAvailableSemaphore = imageAvailableSemaphores_[currentFrame_].Handle();
            const auto renderFinishedSemaphore = renderFinishedSemaphores_[currentFrame_].Handle();

            auto result = vkAcquireNextImageKHR(device_->Handle(), swapChain_->Handle(), noTimeout, imageAvailableSemaphore, nullptr, &currentImageIndex_);

            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            {
                RecreateSwapChain();
                return;
            }

            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            {
                Throw(std::runtime_error(std::string("failed to acquire next image (") + ToString(result) + ")"));
            }

            const auto commandBuffer = commandBuffers_->Begin(currentFrame_);
            gpuTimer_->Reset(commandBuffer);

            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::Render", PERFORMANCEAPI_MAKE_COLOR(200, 200, 255));
                SCOPED_CPU_TIMER("render");
                SCOPED_GPU_TIMER("gpu time");

                ClearViewport(commandBuffer, currentImageIndex_);
                Render(commandBuffer, currentImageIndex_);
                if (DelegatePostRender)
                {
                    DelegatePostRender(commandBuffer, currentImageIndex_);
                }
            }

            commandBuffers_->End(currentFrame_);


            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::UpdateNodes", PERFORMANCEAPI_MAKE_COLOR(255, 200, 255));
                SCOPED_CPU_TIMER("cpugpu-io");
                UpdateUniformBuffer(currentImageIndex_);
            }

            // wait the last frame command buffer to complete
            if (fence)
            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::Fence", PERFORMANCEAPI_MAKE_COLOR(255, 200, 255));
                SCOPED_CPU_TIMER("sync-wait");
                fence->Wait(noTimeout);
            }

            // TODO: 这里可以像uniform buffer一样，和command buffer一样同步进行，但由于有cpu往gpu的同步，因此这里在fence之后，后面根据swapchain来创立多套buffer，就可以同步了
            if (GetScene().UpdateNodes())
            {
                AfterUpdateScene();
            }
            AfterRenderCmd();
            fence = &(inFlightFences_[currentFrame_]);

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            VkCommandBuffer commandBuffers[]{commandBuffer};
            VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
            {
                SCOPED_CPU_TIMER("submit");

                submitInfo.waitSemaphoreCount = 1;
                submitInfo.pWaitSemaphores = waitSemaphores;
                submitInfo.pWaitDstStageMask = waitStages;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = commandBuffers;
                submitInfo.signalSemaphoreCount = 1;
                submitInfo.pSignalSemaphores = signalSemaphores;

                fence->Reset();

                Check(vkQueueSubmit(device_->GraphicsQueue(), 1, &submitInfo, fence->Handle()),
                      "submit draw command buffer");
            }

            {
                PERFORMANCEAPI_INSTRUMENT_COLOR("Renderer::Present", PERFORMANCEAPI_MAKE_COLOR(255, 200, 255));
                SCOPED_CPU_TIMER("present");
                VkSwapchainKHR swapChains[] = {swapChain_->Handle()};
                VkPresentInfoKHR presentInfo = {};
                presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                presentInfo.waitSemaphoreCount = 1;
                presentInfo.pWaitSemaphores = signalSemaphores;
                presentInfo.swapchainCount = 1;
                presentInfo.pSwapchains = swapChains;
                presentInfo.pImageIndices = &currentImageIndex_;
                presentInfo.pResults = nullptr; // Optional

                result = vkQueuePresentKHR(device_->PresentQueue(), &presentInfo);

                AfterPresent();

                if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
                {
                    RecreateSwapChain();
                    return;
                }

                if (result != VK_SUCCESS)
                {
                    Throw(std::runtime_error(std::string("failed to present next image (") + ToString(result) + ")"));
                }
            }

            currentFrame_ = (currentFrame_ + 1) % inFlightFences_.size();
            frameCount_++;
        }
        gpuTimer_->CpuFrameEnd();
    }

    void VulkanBaseRenderer::BeforeNextFrame()
    {
        for (auto& logicRenderer : logicRenderers_)
        {
            logicRenderer.second->BeforeNextFrame();
        }

        if (DelegateBeforeNextTick)
        {
            DelegateBeforeNextTick();
        }
    }

    void VulkanBaseRenderer::RegisterLogicRenderer(ERendererType type)
    {
        switch (type)
        {
        case ERendererType::ERT_PathTracing:
            logicRenderers_[type] = std::make_unique<RayTracing::RayQueryRenderer>(*this);
            break;
        case ERendererType::ERT_Hybrid:
            logicRenderers_[type] = std::make_unique<HybridDeferred::HybridDeferredRenderer>(*this);
            break;
        case ERendererType::ERT_ModernDeferred:
            logicRenderers_[type] = std::make_unique<ModernDeferred::ModernDeferredRenderer>(*this);
            break;
        case ERendererType::ERT_LegacyDeferred:
            logicRenderers_[type] = std::make_unique<LegacyDeferred::LegacyDeferredRenderer>(*this);
            break;
        default:
            assert(false);
        }
        currentLogicRenderer_ = type;
    }

    void VulkanBaseRenderer::SwitchLogicRenderer(ERendererType type)
    {
        currentLogicRenderer_ = type;
    }

    void VulkanBaseRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (logicRenderers_.find(currentLogicRenderer_) != logicRenderers_.end())
        {
            logicRenderers_[currentLogicRenderer_]->Render(commandBuffer, imageIndex);
        }

        if (showWireframe_)
        {
            std::array<VkClearValue, 2> clearValues = {};
            clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clearValues[1].depthStencil = {1.0f, 0};

            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = graphicsPipeline_->RenderPass().Handle();
            renderPassInfo.framebuffer = swapChainFramebuffers_[imageIndex].Handle();
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = swapChain_->Extent();
            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();


            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            {
                auto& scene = GetScene();

                VkDescriptorSet descriptorSets[] = {graphicsPipeline_->DescriptorSet(imageIndex)};
                VkBuffer vertexBuffers[] = {scene.VertexBuffer().Handle()};
                const VkBuffer indexBuffer = scene.IndexBuffer().Handle();
                VkDeviceSize offsets[] = {0};

                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->Handle());
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                // bind the global bindless set
                static const uint32_t k_bindless_set = 1;
                VkDescriptorSet GlobalDescriptorSets[] = {Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0)};
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_->PipelineLayout().Handle(), k_bindless_set,
                                        1, GlobalDescriptorSets, 0, nullptr);
                
                // drawcall one by one
                for (const auto& node : scene.GetNodeProxys())
                {
                    if (node.modelId >= scene.Models().size())
                    {
                        continue;
                    }
                    
                    auto& model = scene.Models()[node.modelId];
                    auto& offset = scene.Offsets()[node.modelId];
                    const auto indexCount = static_cast<uint32_t>(model.NumberOfIndices());

                    // use push constants to set world matrix
                    glm::mat4 worldMatrix = node.worldTS;

                    vkCmdPushConstants(commandBuffer, graphicsPipeline_->PipelineLayout().Handle(), VK_SHADER_STAGE_VERTEX_BIT,
                                       0, sizeof(glm::mat4), &worldMatrix);

                    vkCmdDrawIndexed(commandBuffer, indexCount, 1, offset.r, offset.g, 0);
                }
            }
            vkCmdEndRenderPass(commandBuffer);
        }
    }

    void VulkanBaseRenderer::UpdateUniformBuffer(const uint32_t imageIndex)
    {
        lastUBO = GetUniformBufferObject(swapChain_->RenderOffset(), swapChain_->RenderExtent());
        uniformBuffers_[imageIndex].SetValue(lastUBO);
    }

    void VulkanBaseRenderer::RecreateSwapChain()
    {
        device_->WaitIdle();
        DeleteSwapChain();
        CreateSwapChain();
    }

    LogicRendererBase::LogicRendererBase(VulkanBaseRenderer& baseRender): baseRender_(baseRender)
    {
    }
}
