#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Vulkan/RayTracing/RayTracingProperties.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/Instance.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/WindowSurface.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Loaders/FProcModel.h"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Scene/SceneBuilder.h"

#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Options.hpp"
#include "Engine/Rendering/SoftwareModern/SoftwareModernRenderer.hpp"
#include "Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.hpp"
#include "Engine/Rendering/SoftwareTracing/SoftwareTracingRenderer.hpp"
#include "Engine/Rendering/PathTracing/PathTracingRenderer.hpp"
#include "Engine/Rendering/VoxelTracing/VoxelTracingRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include "Engine/Rendering/GaussianSplat/GaussianSplatPass.hpp"
#include "Engine/Rendering/Upscaler/IUpscaler.hpp"
#include "Engine/Rendering/Upscaler/StreamlineIntegration.hpp"

#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace
{
    constexpr const char* kPortabilitySubsetExtensionName = "VK_KHR_portability_subset";

    void PrintVulkanSdkInformation()
    {
        SPDLOG_INFO("Vulkan SDK Header Version: {}", VK_HEADER_VERSION);
    }
    
    void PrintVulkanDevices(const std::vector<VkPhysicalDevice>& physicalDevices)
    {
        for (const auto& device : physicalDevices)
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

            SPDLOG_INFO("- [{}] {} '{}' ({}: vulkan {} driver {} {} - {})",
                       prop.deviceID, Vulkan::Strings::VendorId(prop.vendorID), prop.deviceName,
                       Vulkan::Strings::DeviceType(prop.deviceType),
                       to_string(vulkanVersion), driverProp.driverName, driverProp.driverInfo,
                       to_string(driverVersion));

            const auto extensions = Vulkan::GetEnumerateVector(device, static_cast<const char*>(nullptr),
                                                               vkEnumerateDeviceExtensionProperties);
            const auto hasRayTracing = std::any_of(extensions.begin(), extensions.end(),
               [](const VkExtensionProperties& extension)
               {
                   return strcmp(extension.extensionName,VK_KHR_RAY_QUERY_EXTENSION_NAME) == 0;
            });
        }
    }

    void PrintVulkanSwapChainInformation(const Vulkan::VulkanBaseRenderer& application)
    {
        const auto& swapChain = application.SwapChain();

        SPDLOG_INFO("Swap Chain: image count: {}, present mode: {}", swapChain.Images().size(),
                   static_cast<int>(swapChain.PresentMode()));
    }

    bool HasDeviceExtension(VkPhysicalDevice physicalDevice, const char* requiredExtension)
    {
        const auto extensions = Vulkan::GetEnumerateVector(physicalDevice, static_cast<const char*>(nullptr),
                                                           vkEnumerateDeviceExtensionProperties);
        return std::any_of(extensions.begin(), extensions.end(),
                           [requiredExtension](const VkExtensionProperties& extension)
                           {
                               return std::strcmp(extension.extensionName, requiredExtension) == 0;
                           });
    }

    bool AddDeviceExtensionIfAvailable(VkPhysicalDevice physicalDevice,
                                       std::vector<const char*>& requiredExtensions,
                                       const char* extensionName,
                                       const char* featureName)
    {
        if (HasDeviceExtension(physicalDevice, extensionName))
        {
            if (std::find(requiredExtensions.begin(), requiredExtensions.end(), extensionName) == requiredExtensions.end())
            {
                requiredExtensions.push_back(extensionName);
            }
            return true;
        }

        SPDLOG_WARN("{} disabled because device extension {} is unavailable", featureName, extensionName);
        return false;
    }

    Rendering::Upscaler::FImageResource MakeRenderImageResource(
        const Vulkan::RenderImage* image,
        VkImageLayout layout,
        VkImageUsageFlags usage)
    {
        if (image == nullptr)
        {
            return {};
        }

        const auto& vkImage = image->GetImage();
        return {
            vkImage.Handle(),
            image->GetImageMemory().Handle(),
            image->GetImageView().Handle(),
            vkImage.Extent(),
            vkImage.Format(),
            layout,
            usage};
    }

    Rendering::Upscaler::FImageResource MakeDepthResource(
        const Vulkan::DepthBuffer& depthBuffer,
        VkExtent2D extent,
        VkImageLayout layout)
    {
        return {
            depthBuffer.GetImage().Handle(),
            depthBuffer.GetImageMemory().Handle(),
            depthBuffer.ImageView().Handle(),
            extent,
            depthBuffer.Format(),
            layout,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT};
    }

    Rendering::Upscaler::FImageResource MakeSwapchainResource(
        const Vulkan::SwapChain& swapChain,
        uint32_t imageIndex,
        VkExtent2D extent,
        VkImageLayout layout)
    {
        if (imageIndex >= swapChain.Images().size() || imageIndex >= swapChain.ImageViews().size())
        {
            return {};
        }

        return {
            swapChain.Images()[imageIndex],
            VK_NULL_HANDLE,
            swapChain.ImageViews()[imageIndex]->Handle(),
            extent,
            swapChain.Format(),
            layout,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT};
    }

}

namespace Vulkan
{
    namespace
    {
        using RendererFactory = std::unique_ptr<LogicRendererBase> (*)(VulkanBaseRenderer&);

        struct RendererDescriptor
        {
            ERendererType type;
            const char* name;
            FRendererRequirements requirements;
            uint32_t referenceColumn;
            uint32_t referenceRow;
            RendererFactory factory;
        };

        template <typename T>
        std::unique_ptr<LogicRendererBase> CreateLogicRenderer(VulkanBaseRenderer& baseRenderer)
        {
            return std::make_unique<T>(baseRenderer);
        }

        const std::array RendererDescriptors{
            RendererDescriptor{ERT_PathTracing, "PathTracing", {true, true}, 1, 1,
                               &CreateLogicRenderer<PathTracing::PathTracingRenderer>},
            RendererDescriptor{ERT_SoftwareTracing, "SoftwareTracing", {true, false}, 1, 0,
                               &CreateLogicRenderer<SoftwareTracing::SoftwareTracingRenderer>},
            RendererDescriptor{ERT_SoftwareModern, "SoftwareModern", {true, false}, 0, 0,
                               &CreateLogicRenderer<SoftwareModern::SoftwareModernRenderer>},
            RendererDescriptor{ERT_VoxelTracing, "VoxelTracing", {true, false}, 0, 1,
                               &CreateLogicRenderer<VoxelTracing::VoxelTracingRenderer>},
            RendererDescriptor{ERT_SoftwareModernNoAmbient, "SoftwareModernNoAmbient", {false, false, true}, 0, 1,
                               &CreateLogicRenderer<SoftwareModernNoAmbient::SoftwareModernNoAmbientRenderer>},
        };

        const RendererDescriptor& GetRendererDescriptor(ERendererType type)
        {
            const auto descriptor = std::find_if(
                RendererDescriptors.begin(), RendererDescriptors.end(),
                [type](const RendererDescriptor& candidate) { return candidate.type == type; });
            assert(descriptor != RendererDescriptors.end());
            return descriptor != RendererDescriptors.end() ? *descriptor : RendererDescriptors.front();
        }
    }

    FRendererRequirements GetRendererRequirements(ERendererType type)
    {
        return GetRendererDescriptor(type).requirements;
    }

    const char* GetRendererName(ERendererType type)
    {
        return GetRendererDescriptor(type).name;
    }

    VulkanBaseRenderer::VulkanBaseRenderer(Vulkan::Window* window, const VkPresentModeKHR presentMode,
                                           const bool enableValidationLayers,
                                           Instance* instance) :
        presentMode_(presentMode)
    {
        ctx_.window = window;
        ctx_.instance.reset(instance);
        ctx_.debugUtilsMessenger.reset(enableValidationLayers
                       ? new DebugUtilsMessenger( *ctx_.instance, VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT)
                       : nullptr);
        ctx_.surface.reset(new Surface(*ctx_.instance));
        caps_.supportDenoiser = false;
        forceSDR_ = GOption->ForceSDR;

        caps_.supportRayTracing = false;
        upscaler_ = Rendering::Upscaler::CreateStreamlineUpscaler();

        // Optional validation path: render the secondary RenderView and composite it as a
        // picture-in-picture inset. Off by default; opt in with GK_MV_DEMO=1.
        const char* mvDemoEnv = std::getenv("GK_MV_DEMO");
        multiViewDemo_ = mvDemoEnv != nullptr && mvDemoEnv[0] == '1';
    }

    VulkanBaseRenderer::~VulkanBaseRenderer()
    {
        VulkanBaseRenderer::DeleteSwapChain();
        DeleteAccelerationStructures();
        rt_.reset();
        ctx_.gpuTimer.reset();
        ctx_.globalTexturePool.reset();
        ctx_.commandPool.reset();
        ctx_.commandPool2.reset();
        ctx_.device.reset();
        ctx_.surface.reset();
        ctx_.debugUtilsMessenger.reset();
        ctx_.instance.reset();
        ctx_.window = nullptr;
    }

    void VulkanBaseRenderer::SelectPhysicalDevice(uint32_t gpuIdx)
    {
        const auto& physicalDevices = ctx_.instance->PhysicalDevices();
        if (gpuIdx >= physicalDevices.size())
        {
            SPDLOG_WARN("Requested GPU index {} is out of range; using GPU 0", gpuIdx);
            gpuIdx = 0;
        }

        VkPhysicalDevice physicalDevice = physicalDevices[gpuIdx];
        VkPhysicalDeviceProperties2 deviceProp{};
        deviceProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(physicalDevice, &deviceProp);

        SPDLOG_INFO("Setting Device [{}]", deviceProp.properties.deviceName);
        SetPhysicalDevice(physicalDevice);
    }

    void VulkanBaseRenderer::SetPhysicalDevice(VkPhysicalDevice physicalDevice)
    {
        if (ctx_.device)
        {
            Throw(std::logic_error("physical device has already been set"));
        }

        std::vector<const char*> requiredExtensions =
        {
            // VK_KHR_swapchain
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };

        VkPhysicalDeviceFeatures deviceFeatures = {};

        VkPhysicalDeviceMemoryProperties memoryProperties = {};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
        VkDeviceSize largestDeviceLocalHeapSize = 0;
        for (uint32_t heapIndex = 0; heapIndex < memoryProperties.memoryHeapCount; ++heapIndex)
        {
            if ((memoryProperties.memoryHeaps[heapIndex].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
            {
                largestDeviceLocalHeapSize =
                    std::max(largestDeviceLocalHeapSize, memoryProperties.memoryHeaps[heapIndex].size);
            }
        }

        const VkDeviceSize perCascadeCount =
            static_cast<VkDeviceSize>(Assets::CUBE_SIZE_XY) * Assets::CUBE_SIZE_XY * Assets::CUBE_SIZE_Z;
        const VkDeviceSize fullAmbientCubeAllocationSize =
            static_cast<VkDeviceSize>(Assets::CUBE_CASCADE_MAX) * perCascadeCount *
                (sizeof(Assets::VoxelData) + sizeof(Assets::AmbientCube)) +
            static_cast<VkDeviceSize>(Assets::ACGI_PAGE_COUNT) * Assets::ACGI_PAGE_COUNT * sizeof(Assets::PageIndex) +
            perCascadeCount * (sizeof(Assets::AmbientCube) + sizeof(glm::u32vec4));
        caps_.fullAmbientCubeBudget = largestDeviceLocalHeapSize >= fullAmbientCubeAllocationSize;
        if (!caps_.fullAmbientCubeBudget)
        {
            SPDLOG_WARN("Largest Vulkan device-local memory heap is {} MB, smaller than full ambient-cube allocation {} MB; ambient-cube renderers will use the no-ambient fallback",
                        static_cast<uint64_t>(largestDeviceLocalHeapSize / (1024 * 1024)),
                        static_cast<uint64_t>(fullAmbientCubeAllocationSize / (1024 * 1024)));
        }

        caps_.supportRayTracing = !GOption->ForceNoRT && ctx_.instance->SupportsRayQuery(physicalDevice);
        if (caps_.supportRayTracing)
        {
            rt_ = std::make_unique<RayTracingResources>();
        }

        // Subgroup support gates the wave fast-path of the soft-mesh GPU cull compute
        // pass (Task.SoftMeshShaderGpuCullCompactWave). We need arithmetic + ballot +
        // basic ops available in the COMPUTE stage; otherwise fall back to the LDS variant.
        {
            VkPhysicalDeviceSubgroupProperties subgroupProps = {};
            subgroupProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
            VkPhysicalDeviceProperties2 props2 = {};
            props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
            props2.pNext = &subgroupProps;
            vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

            const VkSubgroupFeatureFlags requiredOps =
                VK_SUBGROUP_FEATURE_BASIC_BIT | VK_SUBGROUP_FEATURE_ARITHMETIC_BIT | VK_SUBGROUP_FEATURE_BALLOT_BIT;
            caps_.supportSubgroupCull =
                ((subgroupProps.supportedOperations & requiredOps) == requiredOps) &&
                ((subgroupProps.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) != 0);
            SPDLOG_INFO("Soft-mesh GPU cull: subgroup size {}, wave fast-path {}",
                        subgroupProps.subgroupSize, caps_.supportSubgroupCull ? "enabled" : "disabled (LDS fallback)");
        }

        SetPhysicalDeviceImpl(physicalDevice, requiredExtensions, deviceFeatures, nullptr);

        ctx_.globalTexturePool.reset(new Assets::GlobalTexturePool(*ctx_.device, *ctx_.commandPool2, *ctx_.commandPool));

        OnDeviceSet();
        CreateSwapChain();
        // Keep hidden windows hidden (agent validation captures, unit-test engine fixture):
        // showing here would override SDL_WINDOW_HIDDEN and pop a window that steals focus.
        if (!ctx_.window->Config().HiddenWindow)
        {
            ctx_.window->Show();
        }
    }

    void VulkanBaseRenderer::Start()
    {
        // setup vulkan
        PrintVulkanSdkInformation();
        PrintVulkanDevices(ctx_.instance->PhysicalDevices());
        SelectPhysicalDevice(GOption->GpuIdx);
        PrintVulkanSwapChainInformation(*this);
        frame_.currentFrame = 0;
    }

    void VulkanBaseRenderer::End()
    {
        if (ctx_.device)
        {
            ctx_.device->WaitIdle();
        }
        if (upscaler_)
        {
            upscaler_->Shutdown();
        }
        StreamlineWrapper::Shutdown();
        ctx_.gpuTimer.reset();
        ctx_.globalTexturePool.reset();
    }

    void VulkanBaseRenderer::QueueSubmitSignalSemaphore(VkSemaphore semaphore, uint64_t value)
    {
        if (semaphore == VK_NULL_HANDLE)
        {
            return;
        }
        frame_.queuedSignalSemaphores.push_back(semaphore);
        frame_.queuedSignalValues.push_back(value);
    }

    Assets::Scene& VulkanBaseRenderer::GetScene()
    {
        if (activeSceneOverride_ != nullptr)
        {
            return *activeSceneOverride_;
        }
        return *scene_.lock();
    }

    void VulkanBaseRenderer::SetScene(std::shared_ptr<Assets::Scene> scene)
    {
        scene_ = scene;
        PrimaryView().InvalidateTemporalHistory();
        for (auto& secondaryView : secondaryViews_)
        {
            if (secondaryView.view != nullptr)
            {
                secondaryView.view->InvalidateTemporalHistory();
            }
        }
        if (thumbnailRenderView_ != nullptr)
        {
            thumbnailRenderView_->InvalidateTemporalHistory();
            thumbnailRenderView_->SetSceneOverride(nullptr);
        }
        for (auto& referenceView : referenceViews_)
        {
            if (referenceView.second.view != nullptr)
            {
                referenceView.second.view->InvalidateTemporalHistory();
                referenceView.second.view->SetSceneOverride(nullptr);
            }
        }
        materialThumbnailScene_.reset();
        materialThumbnailImages_.clear();
        materialThumbnailHashes_.clear();
        pendingMaterialThumbnails_.clear();
        materialThumbnailSceneReady_ = false;
        meshThumbnailScene_.reset();
        meshThumbnailImages_.clear();
        meshThumbnailHashes_.clear();
        pendingMeshThumbnails_.clear();
        thumbnailVisibilityFrameBuffer_.reset();
        RequestClearAmbientCubeCache();
        resetUpscalerHistory_ = true;
    }

    Rendering::Upscaler::FFrameGenerationState VulkanBaseRenderer::GetFrameGenerationState() const
    {
        return upscaler_ ? upscaler_->FrameGenerationState() : Rendering::Upscaler::FFrameGenerationState{};
    }

    Assets::UniformBufferObject VulkanBaseRenderer::GetUniformBufferObject(
        const VkOffset2D offset, const VkExtent2D extent) const
    {
        if (delegates_.getUniformBufferObject)
        {
            return delegates_.getUniformBufferObject(offset, extent);
        }
        return {};
    }

    VkExtent2D VulkanBaseRenderer::ActiveViewRenderExtent() const
    {
        if (activeViewRenderExtent_.width > 0 && activeViewRenderExtent_.height > 0)
        {
            return activeViewRenderExtent_;
        }
        return frame_.swapChain->RenderExtent();
    }

    void VulkanBaseRenderer::SetSecondaryViewEnabled(uint32_t viewIndex, bool enabled)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }
        secondaryViews_[viewIndex].enabled = enabled;
    }

    bool VulkanBaseRenderer::IsSecondaryViewEnabled(uint32_t viewIndex) const
    {
        return viewIndex < kMaxSecondaryViews && secondaryViews_[viewIndex].enabled;
    }

    void VulkanBaseRenderer::RequestSecondaryViewThisFrame(uint32_t viewIndex)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }
        secondaryViews_[viewIndex].requested = true;
    }

    void VulkanBaseRenderer::SetSecondaryViewRenderExtent(uint32_t viewIndex, VkExtent2D extent)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }

        extent.width = std::max(1u, extent.width);
        extent.height = std::max(1u, extent.height);
        auto& resources = secondaryViews_[viewIndex];
        resources.requestedExtent = extent;
        if (resources.view != nullptr)
        {
            resources.view->SetRenderExtent(extent);
        }
    }

    VkExtent2D VulkanBaseRenderer::SecondaryViewRenderExtent(uint32_t viewIndex) const
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return {};
        }
        return secondaryViews_[viewIndex].requestedExtent;
    }

    void VulkanBaseRenderer::SetSecondaryViewCameraOverride(uint32_t viewIndex, const Assets::Camera& camera)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }

        auto& resources = secondaryViews_[viewIndex];
        const bool changed = !resources.cameraOverride.has_value() ||
            std::memcmp(&resources.cameraOverride->ModelView, &camera.ModelView, sizeof(glm::mat4)) != 0 ||
            resources.cameraOverride->FieldOfView != camera.FieldOfView ||
            resources.cameraOverride->NearPlane != camera.NearPlane ||
            resources.cameraOverride->FarPlane != camera.FarPlane;
        if (changed && resources.view != nullptr)
        {
            resources.view->InvalidateTemporalHistory();
        }
        resources.cameraOverride = camera;
    }

    void VulkanBaseRenderer::ClearSecondaryViewCameraOverride(uint32_t viewIndex)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }
        secondaryViews_[viewIndex].cameraOverride.reset();
    }

    bool VulkanBaseRenderer::HasSecondaryViewCameraOverride(uint32_t viewIndex) const
    {
        return viewIndex < kMaxSecondaryViews && secondaryViews_[viewIndex].cameraOverride.has_value();
    }

    const Assets::UniformBufferObject* VulkanBaseRenderer::SecondaryViewLastUniformBufferObject(uint32_t viewIndex) const
    {
        if (viewIndex >= kMaxSecondaryViews || secondaryViews_[viewIndex].view == nullptr)
        {
            return nullptr;
        }
        return &secondaryViews_[viewIndex].view->State().previousUniformBuffer;
    }

    bool VulkanBaseRenderer::IsSecondaryViewReady(uint32_t viewIndex) const
    {
        return viewIndex < kMaxSecondaryViews && secondaryViews_[viewIndex].offscreenImage != nullptr;
    }

    void VulkanBaseRenderer::SetPhysicalDeviceImpl(
        VkPhysicalDevice physicalDevice,
        std::vector<const char*>& requiredExtensions,
        VkPhysicalDeviceFeatures& deviceFeatures,
        void* nextDeviceFeatures)
    {
        // Ray query / acceleration-structure extensions (conditional on caps_.supportRayTracing).
        // The feature structs must outlive ctx_.device.reset(), so declare them in the outer scope.
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationStructureFeatures = {};
        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures = {};
        if (caps_.supportRayTracing)
        {
            requiredExtensions.insert(requiredExtensions.end(),
                {
                    VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
                    VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
                    VK_KHR_RAY_QUERY_EXTENSION_NAME,
                });
            accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            accelerationStructureFeatures.pNext = nextDeviceFeatures;
            accelerationStructureFeatures.accelerationStructure = true;
            rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
            rayQueryFeatures.pNext = &accelerationStructureFeatures;
            rayQueryFeatures.rayQuery = true;
            nextDeviceFeatures = &rayQueryFeatures;
        }

        // Vulkan Video H.264 encode (remote play hardware path). Probe before device creation;
        // the feature struct must outlive ctx_.device.reset().
        VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2Features = {};
        if (GOption->RemoteMode)
        {
            videoCaps_ = FVulkanVideoCaps::Probe(ctx_.instance->Handle(), physicalDevice);
            videoCaps_.LogSummary();
            if (videoCaps_.Usable())
            {
                requiredExtensions.insert(requiredExtensions.end(),
                    {
                        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
                        VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
                        VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
                        VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME,
                    });
                if (videoCaps_.maintenance1Present)
                {
                    requiredExtensions.push_back(VK_KHR_VIDEO_MAINTENANCE_1_EXTENSION_NAME);
                }
                synchronization2Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR;
                synchronization2Features.pNext = nextDeviceFeatures;
                synchronization2Features.synchronization2 = true;
                nextDeviceFeatures = &synchronization2Features;
            }
            else
            {
                SPDLOG_WARN("RemotePlay: Vulkan Video H.264 encode is not usable on this device; remote mode will "
                            "be unavailable");
            }
        }

        // Remote play uses timeline semaphores to decouple GPU frame completion from the main
        // thread and, later, to bridge graphics -> video encode queue submissions.
        VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures = {};
        const bool requestTimelineSemaphores = GOption->RemoteMode;
        if (requestTimelineSemaphores)
        {
            timelineSemaphoreFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
            timelineSemaphoreFeatures.timelineSemaphore = true;
            timelineSemaphoreFeatures.pNext = nextDeviceFeatures;
            nextDeviceFeatures = &timelineSemaphoreFeatures;
        }

        VkPhysicalDeviceFeatures supportedFeatures = {};
        vkGetPhysicalDeviceFeatures(physicalDevice, &supportedFeatures);

        VkPhysicalDeviceDescriptorIndexingFeatures supportedIndexingFeatures = {};
        supportedIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;

        VkPhysicalDeviceBufferDeviceAddressFeatures supportedBufferDeviceAddressFeatures = {};
        supportedBufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        supportedBufferDeviceAddressFeatures.pNext = &supportedIndexingFeatures;

        VkPhysicalDeviceHostQueryResetFeaturesEXT supportedHostQueryResetFeatures = {};
        supportedHostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
        supportedHostQueryResetFeatures.pNext = &supportedBufferDeviceAddressFeatures;

        VkPhysicalDeviceShaderFloat16Int8FeaturesKHR supportedShaderFloat16Int8Features = {};
        supportedShaderFloat16Int8Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR;
        supportedShaderFloat16Int8Features.pNext = &supportedHostQueryResetFeatures;

        VkPhysicalDeviceShaderDrawParametersFeatures supportedShaderDrawParametersFeatures = {};
        supportedShaderDrawParametersFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
        supportedShaderDrawParametersFeatures.pNext = &supportedShaderFloat16Int8Features;

        VkPhysicalDevice16BitStorageFeatures supportedStorage16BitFeatures = {};
        supportedStorage16BitFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
        supportedStorage16BitFeatures.pNext = &supportedShaderDrawParametersFeatures;

        VkPhysicalDeviceFeatures2 supportedFeatures2 = {};
        supportedFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supportedFeatures2.pNext = &supportedStorage16BitFeatures;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures2);

        VkPhysicalDeviceProperties deviceProperties = {};
        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        auto enableDeviceExtensionIfAvailable = [&](const char* extensionName)
        {
            if (HasDeviceExtension(physicalDevice, extensionName) &&
                std::find(requiredExtensions.begin(), requiredExtensions.end(), extensionName) == requiredExtensions.end())
            {
                requiredExtensions.push_back(extensionName);
            }
        };

        enableDeviceExtensionIfAvailable(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        enableDeviceExtensionIfAvailable(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
        enableDeviceExtensionIfAvailable(VK_KHR_16BIT_STORAGE_EXTENSION_NAME);
        enableDeviceExtensionIfAvailable(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
        enableDeviceExtensionIfAvailable(kPortabilitySubsetExtensionName);
        if (deviceProperties.apiVersion < VK_API_VERSION_1_2 &&
            !HasDeviceExtension(physicalDevice, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
        {
            Throw(std::runtime_error("VK_KHR_buffer_device_address is required"));
        }

        deviceFeatures.fillModeNonSolid = supportedFeatures.fillModeNonSolid;
        deviceFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;
        deviceFeatures.shaderStorageImageReadWithoutFormat = supportedFeatures.shaderStorageImageReadWithoutFormat;
        deviceFeatures.shaderStorageImageWriteWithoutFormat = supportedFeatures.shaderStorageImageWriteWithoutFormat;
        deviceFeatures.shaderInt16 = supportedFeatures.shaderInt16;
        deviceFeatures.shaderInt64 = supportedFeatures.shaderInt64;

        // Optional heatmap instrumentation.
#if WIN32 && GK_ENABLE_SHADER_CLOCK
        requiredExtensions.insert(requiredExtensions.end(),
                                  {
                                      VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
                                      VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME
                                  });

        // Opt-in into mandatory device features.
        VkPhysicalDeviceShaderClockFeaturesKHR shaderClockFeatures = {};
        shaderClockFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
        shaderClockFeatures.pNext = nextDeviceFeatures;
        shaderClockFeatures.shaderSubgroupClock = true;
#endif
        
        // support bindless material
        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
        indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
#if WIN32 && GK_ENABLE_SHADER_CLOCK
        indexingFeatures.pNext = &shaderClockFeatures;
#else
	indexingFeatures.pNext = nextDeviceFeatures;
#endif
        indexingFeatures.runtimeDescriptorArray = supportedIndexingFeatures.runtimeDescriptorArray;
        indexingFeatures.shaderSampledImageArrayNonUniformIndexing = supportedIndexingFeatures.shaderSampledImageArrayNonUniformIndexing;
        indexingFeatures.descriptorBindingPartiallyBound = supportedIndexingFeatures.descriptorBindingPartiallyBound;
        indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = supportedIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind;
        indexingFeatures.descriptorBindingStorageImageUpdateAfterBind = supportedIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind;
        indexingFeatures.descriptorBindingVariableDescriptorCount = supportedIndexingFeatures.descriptorBindingVariableDescriptorCount;


        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
        bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddressFeatures.pNext = &indexingFeatures;
        bufferDeviceAddressFeatures.bufferDeviceAddress = supportedBufferDeviceAddressFeatures.bufferDeviceAddress;

        VkPhysicalDeviceHostQueryResetFeaturesEXT hostQueryResetFeatures = {};
        hostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT;
        hostQueryResetFeatures.pNext = &bufferDeviceAddressFeatures;
        hostQueryResetFeatures.hostQueryReset = supportedHostQueryResetFeatures.hostQueryReset;

        VkPhysicalDeviceShaderFloat16Int8FeaturesKHR shaderFloat16Int8Features = {};
        shaderFloat16Int8Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR;
        shaderFloat16Int8Features.pNext = &hostQueryResetFeatures;
        shaderFloat16Int8Features.shaderFloat16 = supportedShaderFloat16Int8Features.shaderFloat16;
        //shaderFloat16Int8Features.shaderInt8 = true;

        VkPhysicalDeviceShaderDrawParametersFeatures shaderDrawParametersFeatures = {};
        shaderDrawParametersFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES;
        shaderDrawParametersFeatures.pNext = &shaderFloat16Int8Features;
        shaderDrawParametersFeatures.shaderDrawParameters = supportedShaderDrawParametersFeatures.shaderDrawParameters;

        VkPhysicalDevice16BitStorageFeatures storage16BitFeatures = {};
        storage16BitFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES;
        storage16BitFeatures.pNext = &shaderDrawParametersFeatures;
        storage16BitFeatures.storageBuffer16BitAccess = supportedStorage16BitFeatures.storageBuffer16BitAccess;
        storage16BitFeatures.storagePushConstant16 = supportedStorage16BitFeatures.storagePushConstant16;

#if WITH_STREAMLINE
        VkPhysicalDeviceTimelineSemaphoreFeatures streamlineTimelineSemaphoreFeatures = {};
        streamlineTimelineSemaphoreFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
        streamlineTimelineSemaphoreFeatures.timelineSemaphore = true;
        streamlineTimelineSemaphoreFeatures.pNext = &shaderDrawParametersFeatures;
        const auto streamlineCaps = StreamlineWrapper::AppendRequiredDeviceExtensions(physicalDevice, requiredExtensions);
        const bool hasLegacyStreamlineExtensions =
            AddDeviceExtensionIfAvailable(physicalDevice, requiredExtensions,
                                          VK_NVX_BINARY_IMPORT_EXTENSION_NAME, "Streamline binary import") &&
            AddDeviceExtensionIfAvailable(physicalDevice, requiredExtensions,
                                          VK_NVX_IMAGE_VIEW_HANDLE_EXTENSION_NAME, "Streamline image view handles");
        AddDeviceExtensionIfAvailable(physicalDevice, requiredExtensions,
                                      VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME, "Streamline buffer device address");
        if (HasDeviceExtension(physicalDevice, VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME))
        {
            enableDeviceExtensionIfAvailable(VK_EXT_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
        }
        const bool hasStreamlineExtensions =
            streamlineCaps.streamlineInitialized &&
            streamlineCaps.requestedDeviceExtensionsAvailable &&
            hasLegacyStreamlineExtensions;
        if (hasStreamlineExtensions)
        {
            if (!requestTimelineSemaphores)
            {
                storage16BitFeatures.pNext = &streamlineTimelineSemaphoreFeatures;
            }
            caps_.streamlineExtsEnabled = true;
        }
        else
        {
            caps_.streamlineExtsEnabled = false;
        }
#endif

        ctx_.device.reset(new class Device(physicalDevice, *ctx_.surface, requiredExtensions, deviceFeatures,
                                       &storage16BitFeatures));
        ctx_.commandPool.reset(new class CommandPool(*ctx_.device, ctx_.device->GraphicsFamilyIndex(), 0, true));
        ctx_.commandPool2.reset(new class CommandPool(*ctx_.device, ctx_.device->TransferFamilyIndex(), 1, true));
        ctx_.gpuTimer.reset(new VulkanGpuTimer(*ctx_.device, 200, ctx_.device->DeviceProperties()));
    }

    void VulkanBaseRenderer::OnDeviceSet()
    {
        if (upscaler_)
        {
            Rendering::Upscaler::FDeviceInfo deviceInfo{};
            deviceInfo.device = ctx_.device->Handle();
            deviceInfo.instance = ctx_.instance->Handle();
            deviceInfo.physicalDevice = ctx_.device->PhysicalDevice();
            deviceInfo.computeQueueIndex = 0;
            deviceInfo.computeQueueFamily = ctx_.device->ComputeFamilyIndex();
            deviceInfo.graphicsQueueIndex = 0;
            deviceInfo.graphicsQueueFamily = ctx_.device->GraphicsFamilyIndex();
            deviceInfo.opticalFlowQueueIndex = 0;
            deviceInfo.opticalFlowQueueFamily = UINT32_MAX;
            deviceInfo.useNativeOpticalFlowMode = false;

            auto featureCaps = StreamlineWrapper::CachedCaps();
            upscaler_->OnDeviceCreated(deviceInfo, featureCaps);
            caps_.supportDLSS = caps_.streamlineExtsEnabled && featureCaps.supportDLSS;
            caps_.supportDLSSRR = caps_.streamlineExtsEnabled && featureCaps.supportDLSSRR;
            caps_.supportDLSSG = caps_.streamlineExtsEnabled && featureCaps.supportDLSSG;
            caps_.supportReflex = featureCaps.supportReflex;
            caps_.supportPCL = featureCaps.supportPCL;
        }

        if (caps_.supportRayTracing)
        {
            rt_->properties.reset(new RayTracing::RayTracingProperties(Device()));
        }

        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->OnDeviceSet();
        }

        if (delegates_.onDeviceSet)
        {
            delegates_.onDeviceSet();
        }
    }

    void VulkanBaseRenderer::CreateStorageImage(uint32_t bindlessIdx, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, const char* debugName)
    {
        CreateStorageImage(bindlessIdx, frame_.swapChain->RenderExtent(), format, tiling, usage, debugName);
    }

    void VulkanBaseRenderer::CreateStorageImage(uint32_t bindlessIdx, VkExtent2D extent, VkFormat format,
        VkImageTiling tiling, VkImageUsageFlags usage, const char* debugName)
    {
        bindless_.images[bindlessIdx].reset(new RenderImage(Device(), extent, format, tiling, usage, false, debugName));
        if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
        {
            ctx_.globalTexturePool->BindStorageTexture(bindlessIdx, bindless_.images[bindlessIdx]->GetImageView());
        }
    }

    const RenderImage* VulkanBaseRenderer::GetStorageImage(uint32_t bindlessIdx) const
    {
        assert(bindlessIdx < bindless_.images.size());
        return bindless_.images[bindlessIdx].get();
    }

#define CREATE_STORAGE_IMAGE(idx, fmt, tiling, usage) CreateStorageImage(bankBase + Assets::Bindless::idx, extent, fmt, tiling, usage, #idx)

    void VulkanBaseRenderer::CreateRenderTargetBank(uint32_t bankBase)
    {
        CreateRenderTargetBank(bankBase, frame_.swapChain->RenderExtent());
    }

    void VulkanBaseRenderer::CreateRenderTargetBank(uint32_t bankBase, const VkExtent2D extent)
    {
        // Ensure the bindless image vector reaches into this bank's slot range.
        if (bindless_.images.size() < bankBase + Assets::Bindless::RT_COUNT)
        {
            bindless_.images.resize(bankBase + Assets::Bindless::RT_COUNT);
        }

        const VkFormat progressiveHistoryFormat = GOption->HighPrecisionProgressiveHistory
            ? VK_FORMAT_R32G32B32A32_SFLOAT
            : VK_FORMAT_R16G16B16A16_SFLOAT;

        CREATE_STORAGE_IMAGE(RT_ACCUMLATE_DIFFUSE, progressiveHistoryFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_DIFFUSE, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_MINIGBUFFER, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_MINIGBUFFER_DRAW, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT );
        CREATE_STORAGE_IMAGE(RT_OBJEDCTID_0, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        CREATE_STORAGE_IMAGE(RT_OBJEDCTID_1, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_MOTIONVECTOR, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_SHADER_TIMER, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_DENOISED, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_PREV_DEPTHBUFFER, VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_ACCUMLATE_SPECULAR, progressiveHistoryFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_SPECULAR, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_ACCUMLATE_ALBEDO, progressiveHistoryFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_PREV_DIFFUSE, progressiveHistoryFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_PREV_SPECULAR, progressiveHistoryFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_PREV_ALBEDO, progressiveHistoryFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_MOTIONMOMENT, VK_FORMAT_R16_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_DIFFUSE_HITDIST, VK_FORMAT_R16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_SPECULAR_HITDIST, VK_FORMAT_R16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_SPECULAR_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_ATROUS_PING, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_ATROUS_PONG, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_ATROUS_OUT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_ATROUS_SPEC_OUT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_ATROUS_SPEC_PING, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_ATROUS_SPEC_PONG, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_SPLAT_ACCUM, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        CREATE_STORAGE_IMAGE(RT_AMBIENT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        const VkExtent2D gtaoExtent{
            (extent.width + 1u) / 2u,
            (extent.height + 1u) / 2u};
        CreateStorageImage(bankBase + Assets::Bindless::RT_GTAO, gtaoExtent, VK_FORMAT_R16_SFLOAT,
                           VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, "RT_GTAO");
    }
#undef CREATE_STORAGE_IMAGE

    void VulkanBaseRenderer::CreateRenderImages()
    {
        screenshot_.image.reset(new Image(*ctx_.device, frame_.swapChain->Extent(), 1, frame_.swapChain->Format(),
                                         VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT));
        screenshot_.imageMemory.reset(new DeviceMemory(
            screenshot_.image->
            AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
        ctx_.device->DebugUtils().SetObjectName(screenshot_.image->Handle(), "Screenshot Image");
        screenshot_.imageMemory->SetName("Screenshot Memory");

        bindless_.images.resize(Assets::Bindless::RT_COUNT);

        // Primary view RT bank (bank 0 == legacy absolute layout).
        CreateRenderTargetBank(0);
        // Non-primary view resources were destroyed with the swapchain; recreate on demand.
        for (auto& secondaryView : secondaryViews_)
        {
            if (secondaryView.view != nullptr)
            {
                secondaryView.view->SetAllocatedExtent({0, 0});
                secondaryView.view->SetVisibilityFramebuffer(nullptr);
                secondaryView.view->SetCameraAddress(0);
                secondaryView.view->SetPrevDepthValid(false);
                secondaryView.view->ResetCameraUbo();
            }
        }
        if (thumbnailRenderView_ != nullptr)
        {
            thumbnailRenderView_->SetAllocatedExtent({0, 0});
            thumbnailRenderView_->SetVisibilityFramebuffer(nullptr);
            thumbnailRenderView_->SetCameraAddress(0);
            thumbnailRenderView_->SetSceneOverride(nullptr);
            thumbnailRenderView_->SetPrevDepthValid(false);
            thumbnailRenderView_->ResetCameraUbo();
        }
        for (auto& referenceView : referenceViews_)
        {
            auto& resources = referenceView.second;
            resources.visibilityFrameBuffer.reset();
            resources.allocatedExtent = {0, 0};
            if (resources.view != nullptr)
            {
                resources.view->SetAllocatedExtent({0, 0});
                resources.view->SetVisibilityFramebuffer(nullptr);
                resources.view->SetCameraAddress(0);
                resources.view->SetPrevDepthValid(false);
                resources.view->ResetCameraUbo();
            }
        }
        thumbnailVisibilityFrameBuffer_.reset();

        for (uint32_t i = 0; i != frame_.swapChain->Images().size(); i++)
        {
            ctx_.globalTexturePool->BindStorageTexture( Assets::Bindless::RT_SWAPCHAIN0 + i, *frame_.swapChain->ImageViews()[i] );
        }

        frameGeneration_.hudlessImages.clear();
        if (caps_.supportDLSSG)
        {
            frameGeneration_.hudlessImages.reserve(frame_.swapChain->Images().size());
            for (size_t i = 0; i < frame_.swapChain->Images().size(); ++i)
            {
                const std::string debugName = fmt::format("DLSS-G HUD-less {}", i);
                frameGeneration_.hudlessImages.emplace_back(std::make_unique<RenderImage>(
                    Device(),
                    frame_.swapChain->OutputExtent(),
                    frame_.swapChain->Format(),
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    false,
                    debugName.c_str()));
            }
        }
    }

    void VulkanBaseRenderer::CreateSwapChain()
    {
        // 窗口等待
        while (ctx_.window->IsMinimized())
        {
            ctx_.window->WaitForEvents();
        }

        // SwapChain
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();
        const VkPresentModeKHR requestedPresentMode =
            caps_.supportDLSSG && settings.DLSSG
                ? VK_PRESENT_MODE_IMMEDIATE_KHR
                : presentMode_;
        frame_.swapChain.reset(new class SwapChain(*ctx_.device, requestedPresentMode, forceSDR_));
        VkExtent2D renderExtent = frame_.swapChain->Extent();
        if (!GOption->ReferenceMode)
        {
            const bool dlssEnabled = caps_.supportDLSS && settings.DLSS && upscaler_;
            if (dlssEnabled && upscaler_)
            {
                const auto optimal = upscaler_->GetOptimalRenderSettings(
                    settings.SuperResolution,
                    frame_.swapChain->Extent(),
                    dlssEnabled);
                renderExtent = optimal.renderExtent;
            }
            else
            {
                const auto& modeInfo = Rendering::Upscaler::GetUpscaleModeInfo(settings.SuperResolution);
                renderExtent = Rendering::Upscaler::ScaleExtent(frame_.swapChain->Extent(), modeInfo.fallbackScale);
            }
        }

        frame_.swapChain->UpdateRenderViewport(0, 0, renderExtent.width, renderExtent.height);
        frame_.swapChain->UpdateOutputViewport( 0, 0, frame_.swapChain->Extent().width, frame_.swapChain->Extent().height);

        // Primary RenderView mirrors the swapchain's render rect (full-window, bank 0).
        {
            RenderView& primary = renderViews_->Primary();
            primary.SetRenderExtent(frame_.swapChain->RenderExtent());
            primary.SetRenderOffset(frame_.swapChain->RenderOffset());
            primary.SetSubrect(VkRect2D{frame_.swapChain->OutputOffset(),
                                        frame_.swapChain->OutputExtent()});
        }

        // depthBuffer
        frame_.depthBuffer.reset(new class DepthBuffer(*ctx_.commandPool, frame_.swapChain->Extent()));

        // 同步对象
        for (size_t i = 0; i != frame_.swapChain->ImageViews().size(); ++i)
        {
            frame_.imageAvailableSemaphores.emplace_back(*ctx_.device);
            frame_.renderFinishedSemaphores.emplace_back(*ctx_.device);
            frame_.inFlightFences.emplace_back(*ctx_.device, true);
            frame_.inFlightFenceSubmitSerials.emplace_back(0);
            frame_.uniformBuffers.emplace_back(*ctx_.device);
        }

        // commandbuffer
        frame_.commandBuffers.reset(new CommandBuffers(*ctx_.commandPool, static_cast<uint32_t>(frame_.swapChain->ImageViews().size())));

        frame_.currentFence = nullptr;
        frame_.currentFenceSerial = 0;
        frame_.recordingSubmitSerial = 0;
        frame_.queuedSignalSemaphores.clear();
        frame_.queuedSignalValues.clear();

        // 公用RenderImages
        CreateRenderImages();
        PrimaryView().CreateSwapChain(SwapChain());
        for (const auto& view : renderViews_->AdditionalViews())
        {
            view->CreateSwapChain(SwapChain());
        }

        overlay_.wireframePipeline.reset(new class PipelineCommon::GraphicsPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene(), true));
        overlay_.wireframeFrameBuffers.clear();
        overlay_.wireframeFrameBuffers.reserve(frame_.swapChain->ImageViews().size());
        for (const auto& imageView : frame_.swapChain->ImageViews())
        {
            overlay_.wireframeFrameBuffers.emplace_back(frame_.swapChain->Extent(), *imageView, overlay_.wireframePipeline->RenderPass());
        }

        // 公用Pipeline
        overlay_.simpleComposePipeline.reset( new PipelineCommon::ZeroBindCustomPushConstantPipeline(SwapChain(), "assets/shaders/Process.UpScaleFSR.comp.slang.spv", 20));
        overlay_.bufferClearPipeline.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(*frame_.swapChain, "assets/shaders/Util.BufferClear.comp.slang.spv", 4));
        // Shared swap-chain resources must cover every registered renderer because
        // switching logic renderers does not recreate the swap chain.
        if (RegisteredRendererRequirements().requestAmbientCube)
        {
            ambient_.softBake.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.SwAmbientCube.comp.slang.spv", GetScene()));
            ambient_.clearCache.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.ClearAmbientCubeCache.comp.slang.spv", GetScene()));

            if (caps_.supportRayTracing)
            {
                rt_->directLightGenPipeline.reset(new PipelineCommon::ZeroBindWithTLASPipeline(SwapChain(), "assets/shaders/Bake.HwAmbientCube.comp.slang.spv", GetScene()));
            }
        }
        // Voxel sky-visibility bake only needs the voxel SDF (not the cube pool), so it is created
        // for any renderer that requests voxel geometry (e.g. SwModernNoAmbient without ambient cubes).
        if (RegisteredRendererRequirements().NeedsVoxelGeometry())
        {
            ambient_.voxelSkyVisBake.reset(new PipelineCommon::ZeroBindPipeline(
                *frame_.swapChain, "assets/shaders/Bake.VoxelSkyVisibility.comp.slang.spv", GetScene()));
        }
        // Pick the subgroup (wave) fast-path of the GPU cull when the device supports it; otherwise the LDS variant.
        const char* gpuCullSpv = caps_.supportSubgroupCull
            ? "assets/shaders/Task.SoftMeshShaderGpuCullCompactWave.comp.slang.spv"
            : "assets/shaders/Task.SoftMeshShaderGpuCullCompact.comp.slang.spv";
        const char* shadowGpuCullSpv = caps_.supportSubgroupCull
            ? "assets/shaders/Task.SoftMeshShaderShadowGpuCullCompactWave.comp.slang.spv"
            : "assets/shaders/Task.SoftMeshShaderShadowGpuCullCompact.comp.slang.spv";
        overlay_.gpuCullCompactPipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, gpuCullSpv, GetScene()));
        overlay_.softMeshShaderFinalizePipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Task.SoftMeshShaderFinalize.comp.slang.spv", GetScene()));
        overlay_.softMeshShaderExpandPipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Task.SoftMeshShaderExpand.comp.slang.spv", GetScene()));
        overlay_.shadowGpuCullCompactPipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, shadowGpuCullSpv, GetScene()));
        skin_.pipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Task.Skinning.comp.slang.spv", GetScene()));
        overlay_.visualDebuggerPipeline.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(*frame_.swapChain, "assets/shaders/Util.VisualDebugger.comp.slang.spv", 20));

        overlay_.gaussianSplatPass = std::make_unique<GaussianSplat::GaussianSplatPass>(*this);
        overlay_.gaussianSplatPass->CreateResources();

        overlay_.visibilityPipeline.reset(new PipelineCommon::VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));
        overlay_.visibilityFrameBuffer.reset(new FrameBuffer(frame_.swapChain->RenderExtent(), GetViewStorageImage(Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImageView(), overlay_.visibilityPipeline->RenderPass()));

        // 太阳方向光 CSM 阴影 pass + 注册 4 个 cascade 到 Bindless
        {
            overlay_.sunShadowPass.reset(new Shadow::ShadowMapPass(*ctx_.device));
            overlay_.sunShadowPass->CreateResources(GetScene());

            auto* texPool = Assets::GlobalTexturePool::GetInstance();
            for (uint32_t i = 0; i < Assets::Scene::kSunShadowCascadeCount; ++i)
            {
                texPool->BindShadowMap(i, GetScene().SunShadowImageView(i), GetScene().SunShadowSampler());
            }
        }

        // 逻辑Renderer
        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->CreateSwapChain(frame_.swapChain->RenderExtent());
        }

        // Delegate
        if (delegates_.createSwapChain)
        {
            delegates_.createSwapChain();
        }
    }

    void VulkanBaseRenderer::DeleteSwapChain()
    {
        if (upscaler_)
        {
            upscaler_->OnSwapChainDestroyed();
        }

        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->DeleteSwapChain();
        }
        PrimaryView().DeleteSwapChain();
        for (const auto& view : renderViews_->AdditionalViews())
        {
            view->DeleteSwapChain();
        }

        if (delegates_.deleteSwapChain)
        {
            delegates_.deleteSwapChain();
        }

        overlay_.gaussianSplatPass.reset();

        for ( auto& storageImage : bindless_.images )
        {
            storageImage.reset();
        }
        overlay_.visibilityPipeline.reset();
        overlay_.visibilityFrameBuffer.reset();
        // Auxiliary view resources reference view-bank images / the shared render pass; drop them
        // before the swapchain images go away.
        for (auto& secondaryView : secondaryViews_)
        {
            secondaryView.visibilityFrameBuffer.reset();
            secondaryView.offscreenImage.reset();
            secondaryView.offscreenSampler.reset();
            if (secondaryView.view != nullptr)
            {
                secondaryView.view->SetAllocatedExtent({0, 0});
                secondaryView.view->SetVisibilityFramebuffer(nullptr);
                secondaryView.view->SetCameraAddress(0);
                secondaryView.view->SetPrevDepthValid(false);
                secondaryView.view->ResetCameraUbo();
            }
        }
        thumbnailVisibilityFrameBuffer_.reset();
        if (thumbnailRenderView_ != nullptr)
        {
            thumbnailRenderView_->SetAllocatedExtent({0, 0});
            thumbnailRenderView_->SetVisibilityFramebuffer(nullptr);
            thumbnailRenderView_->SetCameraAddress(0);
            thumbnailRenderView_->SetSceneOverride(nullptr);
            thumbnailRenderView_->SetPrevDepthValid(false);
            thumbnailRenderView_->ResetCameraUbo();
        }
        for (auto& referenceView : referenceViews_)
        {
            auto& resources = referenceView.second;
            resources.visibilityFrameBuffer.reset();
            resources.allocatedExtent = {0, 0};
            if (resources.view != nullptr)
            {
                resources.view->SetAllocatedExtent({0, 0});
                resources.view->SetVisibilityFramebuffer(nullptr);
                resources.view->SetCameraAddress(0);
                resources.view->SetPrevDepthValid(false);
                resources.view->ResetCameraUbo();
            }
        }
        overlay_.sunShadowPass.reset();
        
        screenshot_.image.reset();
        screenshot_.imageMemory.reset();
        frameGeneration_.hudlessImages.clear();
        frame_.commandBuffers.reset();
        overlay_.wireframeFrameBuffers.clear();
        overlay_.wireframePipeline.reset();
        overlay_.bufferClearPipeline.reset();
        frame_.inFlightFenceSubmitSerials.clear();
        ambient_.softBake.reset();
        ambient_.voxelSkyVisBake.reset();
        ambient_.clearCache.reset();
        if (rt_)
        {
            rt_->directLightGenPipeline.reset();
        }
        overlay_.gpuCullCompactPipeline.reset();
        overlay_.softMeshShaderFinalizePipeline.reset();
        overlay_.softMeshShaderExpandPipeline.reset();
        overlay_.shadowGpuCullCompactPipeline.reset();
        skin_.pipeline.reset();

        skin_.vertexBuffer.reset();
        skin_.vertexMemory.reset();
        skin_.jointBuffer.reset();
        skin_.jointMemory.reset();

        overlay_.simpleComposePipeline.reset();
        overlay_.visualDebuggerPipeline.reset();
        frame_.uniformBuffers.clear();
        frame_.inFlightFences.clear();
        frame_.renderFinishedSemaphores.clear();
        frame_.imageAvailableSemaphores.clear();
        frame_.queuedSignalSemaphores.clear();
        frame_.queuedSignalValues.clear();
        frame_.depthBuffer.reset();
        frame_.swapChain.reset();

        frame_.currentFence = nullptr;
    }

    void VulkanBaseRenderer::RecreateSwapChain()
    {
        if (upscaler_)
        {
            upscaler_->OnSwapChainDestroyed();
        }
        ctx_.device->WaitIdle();
        DeleteSwapChain();
        CreateSwapChain();
        resetUpscalerHistory_ = true;
    }

    void VulkanBaseRenderer::ReloadShaders()
    {
        RecreateSwapChain();
    }

    void VulkanBaseRenderer::CaptureScreenShot()
    {
        SingleTimeCommands::Submit(CommandPool(), [&](VkCommandBuffer commandBuffer)
        {
            SCOPED_GPU_TIMER("screenshot");
            const auto& image = frame_.swapChain->Images()[frame_.currentImageIndex];

            ImageMemoryBarrier::FullInsert(commandBuffer, image, 0, VK_ACCESS_TRANSFER_READ_BIT,
                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            ImageMemoryBarrier::FullInsert(commandBuffer, screenshot_.image->Handle(), 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                                           VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

            // Copy output image into swap-chain image.
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {SwapChain().Extent().width, SwapChain().Extent().height, 1};

            vkCmdCopyImage(commandBuffer,
                           image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           screenshot_.image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copyRegion);

            ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[frame_.currentImageIndex],
                                           VK_ACCESS_TRANSFER_READ_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        });
    }

    // Camera-independent, runs once per scene per frame (shared across all views).
    void VulkanBaseRenderer::BeginSceneFrame(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        UpdateAccelerationStructuresTop(commandBuffer);
        UpdateSkinningBuffers();
        InitializeBarriers(commandBuffer);
        HandleAmbientCubeCacheInvalidation(commandBuffer, imageIndex);
        DispatchSkinning(commandBuffer, imageIndex);
        UpdateAccelerationStructuresBottom(commandBuffer);
    }

    void VulkanBaseRenderer::RenderViewToBank(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        RenderView& view,
        const bool clearSwapchain,
        LogicRendererBase& logicRenderer)
    {
        SCOPED_GPU_TIMER(view.DebugName());

        const uint32_t previousBankBase = activeViewBankBase_;
        const VkExtent2D previousRenderExtent = activeViewRenderExtent_;
        const VkDeviceAddress previousCameraAddress = activeViewCameraAddress_;
        FrameBuffer* const previousVisibilityFrameBuffer = activeVisibilityFrameBuffer_;
        Assets::Scene* const previousSceneOverride = activeSceneOverride_;
        RenderView* const previousRenderView = activeRenderView_;

        activeSceneOverride_ = view.SceneOverride();
        activeRenderView_ = &view;
        SetActiveViewBankBase(view.RtBankBase());
        SetActiveViewRenderExtent(view.RenderExtent());
        SetActiveViewCameraAddress(view.CameraAddress());
        activeVisibilityFrameBuffer_ = view.VisibilityFramebuffer();

        const bool initializePrevDepthBeforeCull = !view.IsPrimary() && !view.PrevDepthValid();
        if (initializePrevDepthBeforeCull)
        {
            DispatchClearPass(commandBuffer, imageIndex, /*clearSwapchain*/ false);
            GetViewStorageImage(Assets::Bindless::RT_PREV_DEPTHBUFFER)->InsertBarrier(
                commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }

        PreRenderPerView(commandBuffer, imageIndex, clearSwapchain);
        logicRenderer.Render(commandBuffer, imageIndex);
        if (view.CopyObjectIdHistory())
        {
            CopyObjectIdHistory(commandBuffer);
        }
        if (initializePrevDepthBeforeCull)
        {
            view.SetPrevDepthValid(true);
        }

        activeSceneOverride_ = previousSceneOverride;
        activeRenderView_ = previousRenderView;
        activeVisibilityFrameBuffer_ = previousVisibilityFrameBuffer;
        SetActiveViewCameraAddress(previousCameraAddress);
        SetActiveViewRenderExtent(previousRenderExtent);
        SetActiveViewBankBase(previousBankBase);
    }

    void VulkanBaseRenderer::ScheduleRenderView(
        RenderView& view,
        LogicRendererBase& logicRenderer,
        const bool clearSwapchain,
        FRenderViewPostCallback postRender)
    {
        renderViews_->ScheduleView(view, logicRenderer, clearSwapchain, std::move(postRender));
    }

    bool VulkanBaseRenderer::DispatchScheduledRenderViews(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        bool renderedAny = false;
        for (const FRenderViewScheduleItem& item : renderViews_->ScheduledViews())
        {
            if (item.view == nullptr || item.logicRenderer == nullptr)
            {
                continue;
            }

            RenderViewToBank(commandBuffer, imageIndex, *item.view, item.clearSwapchain, *item.logicRenderer);
            renderedAny = true;
            if (item.postRender)
            {
                item.postRender(*item.view);
            }
        }
        renderViews_->ClearSchedule();
        return renderedAny;
    }

    void VulkanBaseRenderer::SetRenderViewUbo(
        RenderView& view,
        const uint32_t imageIndex,
        const Assets::UniformBufferObject& ubo)
    {
        Assets::UniformBuffer& cameraUbo = view.EnsureCameraUbo(
            Device(), imageIndex, static_cast<uint32_t>(frame_.uniformBuffers.size()));
        cameraUbo.SetValue(ubo);
        view.SetCameraAddress(cameraUbo.Buffer().GetDeviceAddress());
    }

    void VulkanBaseRenderer::FinalizeTemporalUbo(RenderView& view, Assets::UniformBufferObject& ubo)
    {
        FViewRenderState& state = view.State();
        const bool hasPrevious = state.previousUniformBuffer.TotalFrames != 0;
        if (!hasPrevious || state.resetHistory)
        {
            ubo.PrevViewProjection = ubo.ViewProjection;
            ubo.PrevViewProjectionUnJit = ubo.ViewProjectionUnJit;
            view.TemporalResolve().InvalidateHistory();
        }
        else
        {
            ubo.PrevViewProjection = state.previousUniformBuffer.ViewProjection;
            ubo.PrevViewProjectionUnJit = state.previousUniformBuffer.ViewProjectionUnJit;
        }
        state.previousUniformBuffer = ubo;
        state.resetHistory = false;
    }

    // Camera-dependent pre-passes; runs per active RenderView into its RT bank (set via
    // SetActiveViewBankBase + activeVisibilityFrameBuffer_ before calling). Only the primary view
    // owns the swapchain, so its clear pass also clears the swapchain image.
    void VulkanBaseRenderer::PreRenderPerView(VkCommandBuffer commandBuffer, const uint32_t imageIndex,
                                              const bool isPrimaryView)
    {
        DispatchGpuCulling(commandBuffer, imageIndex);
        DispatchClearPass(commandBuffer, imageIndex, /*clearSwapchain*/ isPrimaryView);
        DispatchVisibilityPass(commandBuffer, imageIndex);
        DispatchSunShadow(commandBuffer, imageIndex);
    }

    void VulkanBaseRenderer::DrawFrame()
    {
        if (requestRecreateSwapChain_)
        {
            RecreateSwapChain();
            requestRecreateSwapChain_ = false;
            return;
        }

        {
            SCOPED_CPU_TIMER("draw-frame");
            const auto noTimeout = std::numeric_limits<uint64_t>::max();
            const auto& settings = NextEngine::GetInstance()->GetUserSettings();
            const bool frameGenerationEnabled =
                caps_.supportDLSSG &&
                settings.DLSSG &&
                NextEngine::GetInstance()->GetEngineStatus() != NextRenderer::EApplicationStatus::Loading;

            frame_.streamlineFrameToken = upscaler_
                ? upscaler_->BeginFrame(static_cast<uint32_t>(frame_.frameCount),
                                        frameGenerationEnabled,
                                        settings.DLSSGFrameLimitFps)
                : Rendering::Upscaler::FFrameToken{};
            if (upscaler_ && frame_.streamlineFrameToken)
            {
                upscaler_->ReflexSleep(frame_.streamlineFrameToken);
                if (frame_.frameCount > 0 && frame_.frameCount % 60 == 0)
                {
                    upscaler_->UpdateFrameGenerationState();
                }
                upscaler_->MarkFrame(Rendering::Upscaler::EFrameMarker::SimulationStart,
                                     frame_.streamlineFrameToken);
            }

            {
                SCOPED_CPU_TIMER("prepare");
                BeforeNextFrame();
            }
            if (upscaler_ && frame_.streamlineFrameToken)
            {
                upscaler_->MarkFrame(Rendering::Upscaler::EFrameMarker::SimulationEnd,
                                     frame_.streamlineFrameToken);
            }

            {
                SCOPED_CPU_TIMER("hwquery");
                ctx_.gpuTimer->FrameEnd((*frame_.commandBuffers)[frame_.currentImageIndex]);
            }

            // next frame synchronization objects
            const auto imageAvailableSemaphore = frame_.imageAvailableSemaphores[frame_.currentFrame].Handle();
            const auto renderFinishedSemaphore = frame_.renderFinishedSemaphores[frame_.currentFrame].Handle();

            auto result = VkResult(VK_SUCCESS);
            {
                SCOPED_CPU_TIMER("acquire-frame");
                result = StreamlineWrapper::AcquireNextImageKHR(ctx_.device->Handle(), frame_.swapChain->Handle(), noTimeout,
                                                                imageAvailableSemaphore, nullptr, &frame_.currentImageIndex);
            }
            
            if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
            {
                RecreateSwapChain();
                return;
            }

            if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
            {
                Throw(std::runtime_error(std::string("failed to acquire next image (") + ToString(result) + ")"));
            }

            // Wait before CPU-side uniform/stat writes and command-buffer reuse.
            auto* const previousSubmitFence = frame_.currentFence;
            auto* const frameSlotFence = &frame_.inFlightFences[frame_.currentFrame];
            if (previousSubmitFence)
            {
                SCOPED_CPU_TIMER("fence");
                previousSubmitFence->Wait(noTimeout);
                frame_.completedSubmitSerial = std::max(frame_.completedSubmitSerial, frame_.currentFenceSerial);
            }
            if (frameSlotFence != previousSubmitFence)
            {
                SCOPED_CPU_TIMER("frame-slot-fence");
                frameSlotFence->Wait(noTimeout);
                if (frame_.currentFrame < frame_.inFlightFenceSubmitSerials.size())
                {
                    frame_.completedSubmitSerial = std::max(
                        frame_.completedSubmitSerial, frame_.inFlightFenceSubmitSerials[frame_.currentFrame]);
                }
            }
            frame_.currentFence = frameSlotFence;
            frame_.recordingSubmitSerial = frame_.nextSubmitSerial;
            frame_.queuedSignalSemaphores.clear();
            frame_.queuedSignalValues.clear();

            // Runtime node creation can increase the expanded primitive count. Resize only after
            // the previous queue submission has completed so no in-flight work retains old addresses.
            GetScene().EnsureGpuDrivenBufferCapacity(*ctx_.commandPool);

            {
                SCOPED_CPU_TIMER("update uniform");
                UpdateUniformBuffer(frame_.currentImageIndex);
            }

            const auto commandBuffer = frame_.commandBuffers->Begin(frame_.currentFrame);
            ctx_.gpuTimer->Reset(commandBuffer);

            {
                SCOPED_GPU_TIMER("[gpu]");

                {
                    SCOPED_GPU_TIMER("[pre-render]");
                    BeginSceneFrame(commandBuffer, frame_.currentImageIndex);
                    skin_.updateRequests.clear();
                }

                {
                    SCOPED_GPU_TIMER("[render]");
                    Render(commandBuffer, frame_.currentImageIndex);
                }

                {
                    SCOPED_GPU_TIMER("[post-render]");
                    PostRender(commandBuffer, frame_.currentImageIndex);
                }

                if (upscaler_ && frameGenerationEnabled)
                {
                    SCOPED_GPU_TIMER("dlssg-tag");
                    CaptureFrameGenerationHudless(commandBuffer, frame_.currentImageIndex);
                    auto inputs = BuildUpscalerFrameInputs(
                        commandBuffer,
                        frame_.currentImageIndex,
                        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
                    if (frame_.currentImageIndex < frameGeneration_.hudlessImages.size())
                    {
                        inputs.hudlessColor = MakeRenderImageResource(
                            frameGeneration_.hudlessImages[frame_.currentImageIndex].get(),
                            VK_IMAGE_LAYOUT_GENERAL,
                            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
                    }
                    inputs.enableDLSSG = true;
                    upscaler_->TagFrameGeneration(inputs);
                }

                if (delegates_.postRender)
                {
                    SCOPED_GPU_TIMER("imgui");
                    delegates_.postRender(commandBuffer, frame_.currentImageIndex);
                }
            }
            frame_.commandBuffers->End(frame_.currentFrame);

            {
                SCOPED_CPU_TIMER("update nodes");
                if (GetScene().UpdateNodes())
                {
                    AfterUpdateScene();
                }
            }

            VkSubmitInfo submitInfo = {};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

            VkCommandBuffer commandBuffers[]{commandBuffer};
            VkSemaphore waitSemaphores[] = {imageAvailableSemaphore};
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            {
                SCOPED_CPU_TIMER("submit");
                frame_.currentFence = &(frame_.inFlightFences[frame_.currentFrame]);
                {
                    if (upscaler_ && frame_.streamlineFrameToken)
                    {
                        upscaler_->MarkFrame(Rendering::Upscaler::EFrameMarker::RenderSubmitStart,
                                             frame_.streamlineFrameToken);
                    }

                    submitInfo.waitSemaphoreCount = 1;
                    submitInfo.pWaitSemaphores = waitSemaphores;
                    submitInfo.pWaitDstStageMask = waitStages;
                    submitInfo.commandBufferCount = 1;
                    submitInfo.pCommandBuffers = commandBuffers;

                    std::vector<VkSemaphore> signalSemaphores;
                    signalSemaphores.reserve(1 + frame_.queuedSignalSemaphores.size());
                    signalSemaphores.push_back(renderFinishedSemaphore);
                    signalSemaphores.insert(signalSemaphores.end(), frame_.queuedSignalSemaphores.begin(),
                                            frame_.queuedSignalSemaphores.end());

                    submitInfo.signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size());
                    submitInfo.pSignalSemaphores = signalSemaphores.data();

                    std::vector<uint64_t> signalSemaphoreValues;
                    VkTimelineSemaphoreSubmitInfo timelineSubmitInfo = {};
                    if (!frame_.queuedSignalValues.empty())
                    {
                        signalSemaphoreValues.reserve(signalSemaphores.size());
                        signalSemaphoreValues.push_back(0);
                        signalSemaphoreValues.insert(signalSemaphoreValues.end(), frame_.queuedSignalValues.begin(),
                                                     frame_.queuedSignalValues.end());
                        timelineSubmitInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
                        timelineSubmitInfo.signalSemaphoreValueCount =
                            static_cast<uint32_t>(signalSemaphoreValues.size());
                        timelineSubmitInfo.pSignalSemaphoreValues = signalSemaphoreValues.data();
                        submitInfo.pNext = &timelineSubmitInfo;
                    }

                    frame_.currentFence->Reset();

                    Check(vkQueueSubmit(ctx_.device->GraphicsQueue(), 1, &submitInfo, frame_.currentFence->Handle()),
                          "submit draw command buffer");
                    frame_.currentFenceSerial = frame_.recordingSubmitSerial;
                    if (frame_.currentFrame < frame_.inFlightFenceSubmitSerials.size())
                    {
                        frame_.inFlightFenceSubmitSerials[frame_.currentFrame] = frame_.recordingSubmitSerial;
                    }
                    frame_.nextSubmitSerial = frame_.recordingSubmitSerial + 1;

                    if (upscaler_ && frame_.streamlineFrameToken)
                    {
                        upscaler_->MarkFrame(Rendering::Upscaler::EFrameMarker::RenderSubmitEnd,
                                             frame_.streamlineFrameToken);
                    }
                }
            }

            {
                SCOPED_CPU_TIMER("present");
                VkSwapchainKHR swapChains[] = {frame_.swapChain->Handle()};
                VkPresentInfoKHR presentInfo = {};
                presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                presentInfo.waitSemaphoreCount = 1;
                presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
                presentInfo.swapchainCount = 1;
                presentInfo.pSwapchains = swapChains;
                presentInfo.pImageIndices = &frame_.currentImageIndex;
                presentInfo.pResults = nullptr; // Optional

                if (upscaler_ && frame_.streamlineFrameToken)
                {
                    upscaler_->MarkFrame(Rendering::Upscaler::EFrameMarker::PresentStart,
                                         frame_.streamlineFrameToken);
                }

                result = StreamlineWrapper::QueuePresentKHR(ctx_.device->PresentQueue(), &presentInfo);

                if (upscaler_ && frame_.streamlineFrameToken)
                {
                    upscaler_->MarkFrame(Rendering::Upscaler::EFrameMarker::PresentEnd,
                                         frame_.streamlineFrameToken);
                }

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

            frame_.currentFrame = (frame_.currentFrame + 1) % frame_.inFlightFences.size();
            resetUpscalerHistory_ = false;
            frame_.frameCount++;
        }
    }

    void VulkanBaseRenderer::BeforeNextFrame()
    {
        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->BeforeNextFrame();
        }

        if (HasPendingMaterialThumbnail())
        {
            EnsureMaterialThumbnailScene();
        }

        if (delegates_.beforeNextTick)
        {
            delegates_.beforeNextTick();
        }
    }

    void VulkanBaseRenderer::InitializeBarriers(VkCommandBuffer commandBuffer)
    {
        SCOPED_GPU_TIMER("barriers");
        for ( auto& storageImage : bindless_.images )
        {
            if ( storageImage ) storageImage->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        }
    }


    void VulkanBaseRenderer::RegisterLogicRenderer(ERendererType type)
    {
        logicRenderers_.renderers[type] = GetRendererDescriptor(type).factory(*this);
        logicRenderers_.current = type;
    }

    FRendererRequirements VulkanBaseRenderer::CurrentRendererRequirements() const
    {
        const auto renderer = logicRenderers_.renderers.find(logicRenderers_.current);
        if (renderer != logicRenderers_.renderers.end())
        {
            return renderer->second->Requirements();
        }

        return GetRendererRequirements(logicRenderers_.current);
    }

    FRendererRequirements VulkanBaseRenderer::RegisteredRendererRequirements() const
    {
        FRendererRequirements requirements;
        for (const auto& logicRenderer : logicRenderers_.renderers)
        {
            requirements.Merge(logicRenderer.second->Requirements());
        }
        return requirements;
    }

    void VulkanBaseRenderer::SwitchLogicRenderer(ERendererType type)
    {
        logicRenderers_.current = type;
    }

    void VulkanBaseRenderer::ComposeViewToSwapchainSubrect(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        RenderView& view)
    {
        SCOPED_GPU_TIMER("resolve pass");
        SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);

        const uint32_t previousBankBase = activeViewBankBase_;
        SetActiveViewBankBase(view.RtBankBase());
        GetViewStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(
            commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);

        const VkRect2D subrect = view.Desc().subrect;
        const std::array<uint32_t, 5> pushConst{
            imageIndex,
            static_cast<uint32_t>(std::max(0, subrect.offset.x)),
            static_cast<uint32_t>(std::max(0, subrect.offset.y)),
            subrect.extent.width,
            subrect.extent.height};

        overlay_.simpleComposePipeline->BindPipeline(commandBuffer, pushConst.data());

        vkCmdDispatch(
            commandBuffer,
            Utilities::Math::GetSafeDispatchCount(subrect.extent.width, 8),
            Utilities::Math::GetSafeDispatchCount(subrect.extent.height, 8), 1);
        SetActiveViewBankBase(previousBankBase);
    }

    void VulkanBaseRenderer::ResolvePrimaryViewToSwapchain(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("resolve pass");

        SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);
        GetViewStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(
            commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);

        bool resolvedByUpscaler = false;
        if (upscaler_ && SupportDLSS() && NextEngine::GetInstance()->GetUserSettings().DLSS)
        {
            resolvedByUpscaler = upscaler_->Evaluate(
                BuildUpscalerFrameInputs(commandBuffer, imageIndex, VK_IMAGE_LAYOUT_GENERAL));
        }
        if (resolvedByUpscaler)
        {
            return;
        }

        const bool fsrEnabled = NextEngine::GetInstance()->GetUserSettings().FSR;
        if (fsrEnabled)
        {
            const std::array<uint32_t, 5> pushConst = {
                imageIndex,
                uint32_t(SwapChain().OutputOffset().x),
                uint32_t(SwapChain().OutputOffset().y),
                uint32_t(SwapChain().OutputExtent().width),
                uint32_t(SwapChain().OutputExtent().height)
            };
            overlay_.simpleComposePipeline->BindPipeline(commandBuffer, pushConst.data());

            vkCmdDispatch(
                commandBuffer,
                Utilities::Math::GetSafeDispatchCount(SwapChain().OutputExtent().width, 8),
                Utilities::Math::GetSafeDispatchCount(SwapChain().OutputExtent().height, 8), 1);
        }
        else
        {
            VkImageBlit blitRegion = {};
            blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blitRegion.srcOffsets[0] = {0, 0, 0};
            blitRegion.srcOffsets[1] = {
                static_cast<int32_t>(SwapChain().RenderExtent().width),
                static_cast<int32_t>(SwapChain().RenderExtent().height),
                1
            };
            blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blitRegion.dstOffsets[0] = {
                static_cast<int32_t>(SwapChain().OutputOffset().x),
                static_cast<int32_t>(SwapChain().OutputOffset().y),
                0
            };
            blitRegion.dstOffsets[1] = {
                static_cast<int32_t>(SwapChain().OutputOffset().x + SwapChain().OutputExtent().width),
                static_cast<int32_t>(SwapChain().OutputOffset().y + SwapChain().OutputExtent().height),
                1
            };

            vkCmdBlitImage(commandBuffer,
                           GetViewStorageImage(Assets::Bindless::RT_DENOISED)->GetImage().Handle(),
                           VK_IMAGE_LAYOUT_GENERAL,
                           SwapChain().Images()[imageIndex],
                           VK_IMAGE_LAYOUT_GENERAL,
                           1,
                           &blitRegion,
                           VK_FILTER_LINEAR);
        }
    }

    void VulkanBaseRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const auto preparePrimaryView = [this](const char* debugName) -> RenderView&
        {
            RenderView& primary = PrimaryView();
            primary.SetDebugName(debugName);
            primary.SetRenderExtent(frame_.swapChain->RenderExtent());
            primary.SetRenderOffset(frame_.swapChain->RenderOffset());
            primary.SetSubrect(VkRect2D{frame_.swapChain->OutputOffset(), frame_.swapChain->OutputExtent()});
            primary.SetVisibilityFramebuffer(nullptr);
            primary.SetSceneOverride(nullptr);
            primary.SetCopyObjectIdHistory(true);
            return primary;
        };

        renderViews_->ClearSchedule();
        if (GOption->ReferenceMode)
        {
            static constexpr std::array<ERendererType, 4> kReferenceRendererTypes{
                ERT_SoftwareModern,
                ERT_SoftwareTracing,
                ERT_SoftwareModernNoAmbient,
                ERT_PathTracing,
            };

            bool clearSwapchainForReference = true;
            bool renderedAnyReferenceView = false;
            for (const ERendererType rendererType : kReferenceRendererTypes)
            {
                const auto logicRenderer = logicRenderers_.renderers.find(rendererType);
                if (logicRenderer == logicRenderers_.renderers.end())
                {
                    continue;
                }

                RenderView& referenceView = EnsureReferenceView(rendererType, imageIndex);
                ScheduleRenderView(
                    referenceView,
                    *logicRenderer->second,
                    clearSwapchainForReference,
                    [this, commandBuffer, imageIndex](RenderView& view)
                    {
                        ComposeViewToSwapchainSubrect(commandBuffer, imageIndex, view);
                    });
                clearSwapchainForReference = false;
                renderedAnyReferenceView = true;
            }

            DispatchScheduledRenderViews(commandBuffer, imageIndex);
            if (renderedAnyReferenceView)
            {
                SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
            }
        }
        else
        {
            const auto renderer = logicRenderers_.renderers.find(logicRenderers_.current);
            if (renderer != logicRenderers_.renderers.end())
            {
                RenderView& primary = preparePrimaryView("primary view");
                ScheduleRenderView(primary, *renderer->second, /*clearSwapchain*/ true);
                DispatchScheduledRenderViews(commandBuffer, imageIndex);
            }

            if (overlay_.gaussianSplatPass)
            {
                SCOPED_GPU_TIMER("Gaussian splats");
                overlay_.gaussianSplatPass->Execute(commandBuffer, imageIndex);
            }

            ResolvePrimaryViewToSwapchain(commandBuffer, imageIndex);

            // Swapchain is in GENERAL here (before the present barrier). Auxiliary views render
            // through the same RenderViewManager schedule, then copy/compose their outputs.
            const bool hasSecondaryViewWork = std::any_of(
                secondaryViews_.begin(), secondaryViews_.end(),
                [](const SecondaryRenderViewResources& view)
                {
                    return view.enabled || view.requested;
                });
            if (multiViewDemo_ || hasSecondaryViewWork || HasPendingThumbnail())
            {
                ScheduleAuxiliaryViews(commandBuffer, imageIndex);
                DispatchScheduledRenderViews(commandBuffer, imageIndex);
            }
            for (auto& secondaryView : secondaryViews_)
            {
                secondaryView.requested = false;
            }

            if (NextEngine::GetInstance()->GetShowFlags().ShowWireframe)
            {
                DrawWireframeOverlay(commandBuffer, imageIndex);
            }
            else
            {
                SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
            }
        }
    }

    VkDeviceAddress VulkanBaseRenderer::ActiveViewCameraAddress(const uint32_t imageIndex) const
    {
        if (activeViewCameraAddress_ != 0)
        {
            return activeViewCameraAddress_;
        }
        return frame_.uniformBuffers[imageIndex].Buffer().GetDeviceAddress();
    }

    namespace
    {
        Assets::UniformBufferObject MakeCameraUbo(const Assets::UniformBufferObject& base,
                                                  const Assets::Camera& camera,
                                                  const Assets::EnvironmentSetting& env,
                                                  VkExtent2D extent)
        {
            Assets::UniformBufferObject ubo = base;
            ubo.ModelView = camera.ModelView;
            ubo.ModelViewInverse = glm::inverse(camera.ModelView);
            const float aspect = static_cast<float>(std::max(1u, extent.width)) /
                static_cast<float>(std::max(1u, extent.height));
            ubo.Projection =
                glm::perspective(glm::radians(camera.FieldOfView), aspect, camera.NearPlane, camera.FarPlane);
            ubo.Projection[1][1] *= -1.0f;
            ubo.ProjectionUnJit = ubo.Projection;
            ubo.ProjectionInverse = glm::inverse(ubo.Projection);
            ubo.ProjectionInverseUnJit = ubo.ProjectionInverse;
            ubo.ViewProjection = ubo.Projection * camera.ModelView;
            ubo.ViewProjectionUnJit = ubo.ProjectionUnJit * camera.ModelView;
            ubo.PrevViewProjection = ubo.ViewProjection;
            ubo.PrevViewProjectionUnJit = ubo.ViewProjectionUnJit;
            ubo.Jitter = glm::vec4(0.0f);
            ubo.ViewportRect = glm::vec4(
                0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height));

            const auto cascades = env.ComputeSunCascades(
                ubo.ViewProjectionUnJit, camera.NearPlane, camera.FarPlane, 400.0f);
            for (uint32_t i = 0; i < Assets::Scene::kSunShadowCascadeCount; ++i)
            {
                ubo.SunCascadeViewProjection[i] = cascades.viewProjection[i];
            }
            ubo.CascadeSplits = cascades.splits;

            ubo.TemporalFrames = 1;
            ubo.TotalFrames = std::max(1u, base.TotalFrames);
            ubo.TAA = false;
            ubo.ProgressiveRender = false;
            return ubo;
        }

        // Build a second camera by orbiting the primary eye around its look-at point about world-Y.
        Assets::UniformBufferObject MakeOrbitedCameraUbo(const Assets::UniformBufferObject& base,
                                                         const Assets::Camera& camera,
                                                         const Assets::EnvironmentSetting& env,
                                                         VkExtent2D extent,
                                                         float yawDegrees, float pivotDistance)
        {
            Assets::Camera orbitedCamera = camera;
            const glm::mat4 c2w = base.ModelViewInverse;
            const glm::vec3 eye = glm::vec3(c2w[3]);
            const glm::vec3 forward = -glm::normalize(glm::vec3(c2w[2]));
            const glm::vec3 pivot = eye + forward * pivotDistance;
            const glm::mat4 rot = glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3(0, 1, 0));
            const glm::vec3 newEye = pivot + glm::vec3(rot * glm::vec4(eye - pivot, 1.0f));
            orbitedCamera.ModelView = glm::lookAt(newEye, pivot, glm::vec3(0, 1, 0));
            return MakeCameraUbo(base, orbitedCamera, env, extent);
        }
    }

    uint32_t VulkanBaseRenderer::RequestMaterialThumbnail(const uint32_t materialIndex, const uint64_t materialHash)
    {
        if (materialIndex >= kMaterialThumbnailMaxSlots)
        {
            return std::numeric_limits<uint32_t>::max();
        }

        if (materialThumbnailHashes_.size() <= materialIndex)
        {
            materialThumbnailHashes_.resize(static_cast<size_t>(materialIndex) + 1, 0);
        }
        if (materialThumbnailImages_.size() <= materialIndex)
        {
            materialThumbnailImages_.resize(static_cast<size_t>(materialIndex) + 1);
        }

        if (materialThumbnailImages_[materialIndex] != nullptr &&
            materialThumbnailHashes_[materialIndex] == materialHash)
        {
            return kMaterialThumbnailSampleSlotBase + materialIndex;
        }

        materialThumbnailHashes_[materialIndex] = materialHash;
        if (std::find(pendingMaterialThumbnails_.begin(), pendingMaterialThumbnails_.end(), materialIndex) ==
            pendingMaterialThumbnails_.end())
        {
            pendingMaterialThumbnails_.push_back(materialIndex);
        }
        return std::numeric_limits<uint32_t>::max();
    }

    uint32_t VulkanBaseRenderer::RequestMeshThumbnail(const uint32_t modelIndex, const uint64_t modelHash)
    {
        if (modelIndex >= kMeshThumbnailMaxSlots)
        {
            return std::numeric_limits<uint32_t>::max();
        }

        if (meshThumbnailHashes_.size() <= modelIndex)
        {
            meshThumbnailHashes_.resize(static_cast<size_t>(modelIndex) + 1, 0);
        }
        if (meshThumbnailImages_.size() <= modelIndex)
        {
            meshThumbnailImages_.resize(static_cast<size_t>(modelIndex) + 1);
        }

        if (meshThumbnailImages_[modelIndex] != nullptr && meshThumbnailHashes_[modelIndex] == modelHash)
        {
            return kMeshThumbnailSampleSlotBase + modelIndex;
        }

        meshThumbnailHashes_[modelIndex] = modelHash;
        if (std::find(pendingMeshThumbnails_.begin(), pendingMeshThumbnails_.end(), modelIndex) ==
            pendingMeshThumbnails_.end())
        {
            pendingMeshThumbnails_.push_back(modelIndex);
        }
        return std::numeric_limits<uint32_t>::max();
    }

    RenderView& VulkanBaseRenderer::EnsureReferenceView(const ERendererType type, const uint32_t imageIndex)
    {
        const RendererDescriptor& descriptor = GetRendererDescriptor(type);
        VkExtent2D extent{
            std::max(1u, frame_.swapChain->RenderExtent().width / 2u),
            std::max(1u, frame_.swapChain->RenderExtent().height / 2u)};
        VkOffset2D offset{
            static_cast<int32_t>(descriptor.referenceColumn * extent.width),
            static_cast<int32_t>(descriptor.referenceRow * extent.height)};

        auto& resources = referenceViews_[type];
        if (resources.view == nullptr)
        {
            FViewDesc viewDesc{};
            viewDesc.renderExtent = extent;
            viewDesc.outputKind = EViewOutputKind::SwapchainSubrect;
            viewDesc.schedule = EViewSchedule::Persistent;
            viewDesc.subrect = VkRect2D{offset, extent};
            resources.view = renderViews_->CreateView(viewDesc, descriptor.name);
            if (resources.view == nullptr)
            {
                Throw(std::runtime_error("failed to allocate reference RenderView bank"));
            }
            resources.view->CreateSwapChain(SwapChain());
        }

        RenderView& view = *resources.view;
        view.SetDebugName(descriptor.name);
        view.SetRenderExtent(extent);
        view.SetRenderOffset({0, 0});
        view.SetSubrect(VkRect2D{offset, extent});
        view.SetSceneOverride(nullptr);
        view.SetCopyObjectIdHistory(true);

        if (resources.allocatedExtent.width != extent.width ||
            resources.allocatedExtent.height != extent.height)
        {
            resources.visibilityFrameBuffer.reset();
            CreateRenderTargetBank(view.RtBankBase(), extent);
            resources.visibilityFrameBuffer.reset(new FrameBuffer(
                extent,
                GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImageView(),
                overlay_.visibilityPipeline->RenderPass()));
            resources.allocatedExtent = extent;
            view.SetAllocatedExtent(extent);
            view.SetPrevDepthValid(false);
        }

        Assets::UniformBufferObject ubo = frame_.lastUBO;
        ubo.ViewportRect = glm::vec4(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height));
        ubo.Jitter = glm::vec4(0.0f);
        ubo.TemporalFrames = 1;
        ubo.TAA = false;
        ubo.ProgressiveRender = false;
        FinalizeTemporalUbo(view, ubo);

        SetRenderViewUbo(view, imageIndex, ubo);
        view.SetVisibilityFramebuffer(resources.visibilityFrameBuffer.get());
        return view;
    }

    RenderView& VulkanBaseRenderer::EnsureSecondaryRenderView(uint32_t viewIndex)
    {
        viewIndex = std::min(viewIndex, kMaxSecondaryViews - 1);
        auto& resources = secondaryViews_[viewIndex];

        VkExtent2D extent = resources.requestedExtent;
        if (extent.width == 0 || extent.height == 0)
        {
            extent = frame_.swapChain->RenderExtent();
        }
        extent.width = std::max(1u, extent.width);
        extent.height = std::max(1u, extent.height);

        if (resources.view == nullptr)
        {
            FViewDesc viewDesc{};
            viewDesc.renderExtent = extent;
            viewDesc.outputKind = EViewOutputKind::OffscreenTexture;
            viewDesc.schedule = EViewSchedule::Persistent;
            resources.view = renderViews_->CreateView(viewDesc, fmt::format("secondary view {}", viewIndex));
            if (resources.view == nullptr)
            {
                Throw(std::runtime_error("failed to allocate secondary RenderView bank"));
            }
            resources.view->CreateSwapChain(SwapChain());
        }
        resources.view->SetDebugName(fmt::format("secondary view {}", viewIndex));
        resources.view->SetRenderExtent(extent);
        resources.view->SetCopyObjectIdHistory(true);

        if (resources.view->AllocatedExtent().width == extent.width &&
            resources.view->AllocatedExtent().height == extent.height &&
            resources.view->VisibilityFramebuffer() != nullptr &&
            resources.offscreenImage != nullptr)
        {
            return *resources.view;
        }

        const uint32_t bank = resources.view->RtBankBase();
        resources.visibilityFrameBuffer.reset();
        resources.offscreenImage.reset();
        CreateRenderTargetBank(bank, extent);
        // Per-view visibility framebuffer: this view's RT_MINIGBUFFER_DRAW as color; depth comes from the
        // shared render pass (primary depth) — safe because views render sequentially.
        resources.visibilityFrameBuffer.reset(new FrameBuffer(
            extent,
            GetStorageImage(bank + Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImageView(),
            overlay_.visibilityPipeline->RenderPass()));
        resources.view->SetVisibilityFramebuffer(resources.visibilityFrameBuffer.get());

        // Offscreen sampled copy of the secondary view's composed output, bound into the sample
        // array for ImGui display. Created once; bind the descriptor once (image is stable).
        const std::string offscreenDebugName = fmt::format("Secondary View {} Offscreen", viewIndex);
        resources.offscreenImage.reset(new RenderImage(
            Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            false, offscreenDebugName.c_str()));
        if (!resources.offscreenSampler)
        {
            Vulkan::SamplerConfig samplerConfig{};
            samplerConfig.AddressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerConfig.AddressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerConfig.AnisotropyEnable = false;
            resources.offscreenSampler.reset(new Vulkan::Sampler(*ctx_.device, samplerConfig));
        }
        ctx_.globalTexturePool->BindSampleTexture(SecondaryViewSampleSlot(viewIndex),
            resources.offscreenImage->GetImageView(), *resources.offscreenSampler);

        resources.view->SetAllocatedExtent(extent);
        resources.view->SetPrevDepthValid(false);
        return *resources.view;
    }

    void VulkanBaseRenderer::EnsureMaterialThumbnailScene()
    {
        if (materialThumbnailSceneReady_)
        {
            return;
        }

        materialThumbnailScene_ = std::make_unique<Assets::Scene>(
            CommandPool(), false, /*allocateAmbientResources*/ false, /*enableCpuAcceleration*/ false);

        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;

        models.push_back(Assets::FProcModel::CreateSphere(glm::vec3(0.0f), 1.0f));
        Assets::FMaterial previewMaterial;
        previewMaterial.gpuMaterial_ = Assets::Material::Lambertian(glm::vec3(0.75f));
        previewMaterial.name_ = "__material_thumbnail_preview";
        materials.push_back(previewMaterial);
        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            "__MaterialThumbnailSphere",
            glm::vec3(0.0f),
            glm::vec3(1.0f),
            1u,
            0u,
            0u,
            true));

        materialThumbnailScene_->Reload(nodes, models, materials, lights, tracks);
        materialThumbnailScene_->PostLoad(skeletons);
        materialThumbnailScene_->RebuildMeshBuffer(CommandPool(), false);

        Assets::EnvironmentSetting env;
        env.Reset();
        env.HasSky = true;
        env.HasSun = false;
        env.SunIntensity = 1000.0f;
        env.SunRotation = 0.35f;
        env.SkyIntensity = 300.0f;
        materialThumbnailScene_->SetEnvSettings(env);

        Assets::Camera camera{};
        camera.name = "Material Thumbnail";
        camera.FieldOfView = 38.0f;
        camera.Aperture = 0.0f;
        camera.FocalDistance = 3.0f;
        camera.NearPlane = 0.01f;
        camera.FarPlane = 20.0f;
        camera.ModelView = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        materialThumbnailScene_->SetRenderCamera(camera);
        materialThumbnailScene_->UpdateNodes();
        materialThumbnailSceneReady_ = true;
    }

    void VulkanBaseRenderer::EnsureThumbnailRenderTarget()
    {
        if (thumbnailRenderView_ == nullptr)
        {
            FViewDesc viewDesc{};
            viewDesc.renderExtent = kThumbnailExtent;
            viewDesc.outputKind = EViewOutputKind::OffscreenTexture;
            viewDesc.schedule = EViewSchedule::Transient;
            thumbnailRenderView_ = renderViews_->CreateView(viewDesc, "thumbnail view");
            if (thumbnailRenderView_ == nullptr)
            {
                Throw(std::runtime_error("failed to allocate thumbnail RenderView bank"));
            }
            thumbnailRenderView_->CreateSwapChain(SwapChain());
            thumbnailRenderView_->SetCopyObjectIdHistory(false);
        }
        thumbnailRenderView_->SetRenderExtent(kThumbnailExtent);
        thumbnailRenderView_->SetCopyObjectIdHistory(false);

        if (thumbnailRenderView_->AllocatedExtent().width != kThumbnailExtent.width ||
            thumbnailRenderView_->AllocatedExtent().height != kThumbnailExtent.height ||
            thumbnailRenderView_->VisibilityFramebuffer() == nullptr)
        {
            const uint32_t thumbnailBank = thumbnailRenderView_->RtBankBase();
            CreateRenderTargetBank(thumbnailBank, kThumbnailExtent);
            thumbnailVisibilityFrameBuffer_.reset(new FrameBuffer(
                kThumbnailExtent,
                GetStorageImage(thumbnailBank + Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImageView(),
                overlay_.visibilityPipeline->RenderPass()));
            thumbnailRenderView_->SetVisibilityFramebuffer(thumbnailVisibilityFrameBuffer_.get());
            thumbnailRenderView_->SetAllocatedExtent(kThumbnailExtent);
            thumbnailRenderView_->SetPrevDepthValid(false);
        }
        if (!thumbnailSampler_)
        {
            Vulkan::SamplerConfig samplerConfig{};
            samplerConfig.AddressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerConfig.AddressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerConfig.AnisotropyEnable = false;
            thumbnailSampler_.reset(new Vulkan::Sampler(*ctx_.device, samplerConfig));
        }
    }

    void VulkanBaseRenderer::RebuildMeshThumbnailScene(const Assets::Model& model)
    {
        meshThumbnailScene_ = std::make_unique<Assets::Scene>(
            CommandPool(), false, /*allocateAmbientResources*/ false, /*enableCpuAcceleration*/ false);

        const glm::vec3 aabbMin = model.GetLocalAABBMin();
        const glm::vec3 aabbMax = model.GetLocalAABBMax();
        const glm::vec3 center = (aabbMin + aabbMax) * 0.5f;
        const glm::vec3 extent = glm::max(aabbMax - aabbMin, glm::vec3(0.001f));
        const float radius = std::max(glm::length(extent) * 0.5f, 0.05f);

        std::vector<std::shared_ptr<Assets::Node>> nodes;
        std::vector<Assets::Model> models;
        std::vector<Assets::FMaterial> materials;
        std::vector<Assets::LightObject> lights;
        std::vector<Assets::AnimationTrack> tracks;
        std::vector<Assets::Skeleton> skeletons;

        models.push_back(model);
        Assets::FMaterial previewMaterial;
        previewMaterial.gpuMaterial_ = Assets::Material::Lambertian(glm::vec3(0.72f, 0.74f, 0.78f));
        previewMaterial.name_ = "__mesh_thumbnail_default";
        materials.push_back(previewMaterial);
        nodes.push_back(Assets::SceneBuilder::CreateRenderNode(
            "__MeshThumbnailModel",
            -center,
            glm::vec3(1.0f),
            1u,
            0u,
            0u,
            true));

        meshThumbnailScene_->Reload(nodes, models, materials, lights, tracks);
        meshThumbnailScene_->PostLoad(skeletons);
        meshThumbnailScene_->RebuildMeshBuffer(CommandPool(), false);

        Assets::EnvironmentSetting env;
        env.Reset();
        env.HasSky = true;
        env.HasSun = false;
        env.SunIntensity = 500.0f;
        env.SunRotation = 0.35f;
        env.SkyIntensity = 300.0f;
        meshThumbnailScene_->SetEnvSettings(env);

        Assets::Camera camera{};
        camera.name = "Mesh Thumbnail";
        camera.FieldOfView = 38.0f;
        camera.Aperture = 0.0f;
        const float halfFov = glm::radians(camera.FieldOfView) * 0.5f;
        const float distance = std::max(radius / std::max(std::sin(halfFov), 0.01f) * 1.18f, 0.15f);
        const glm::vec3 viewDir = glm::normalize(glm::vec3(0.72f, 0.42f, 0.86f));
        const glm::vec3 eye = viewDir * distance;
        camera.FocalDistance = distance;
        camera.NearPlane = std::max(0.001f, distance - radius * 2.5f);
        camera.FarPlane = std::max(camera.NearPlane + 1.0f, distance + radius * 3.5f);
        camera.ModelView = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        meshThumbnailScene_->SetRenderCamera(camera);
        meshThumbnailScene_->UpdateNodes();
    }

    Assets::UniformBufferObject VulkanBaseRenderer::BuildThumbnailUbo(Assets::Scene& scene, const VkExtent2D extent) const
    {
        Assets::UniformBufferObject ubo{};

        const Assets::Camera camera = scene.GetRenderCamera();
        const float aspect = static_cast<float>(std::max(1u, extent.width)) /
            static_cast<float>(std::max(1u, extent.height));
        ubo.ModelView = camera.ModelView;
        ubo.Projection = glm::perspective(glm::radians(camera.FieldOfView), aspect, camera.NearPlane, camera.FarPlane);
        ubo.Projection[1][1] *= -1.0f;
        ubo.ProjectionUnJit = ubo.Projection;
        ubo.ModelViewInverse = glm::inverse(ubo.ModelView);
        ubo.ProjectionInverse = glm::inverse(ubo.Projection);
        ubo.ProjectionInverseUnJit = ubo.ProjectionInverse;
        ubo.ViewProjection = ubo.Projection * ubo.ModelView;
        ubo.ViewProjectionUnJit = ubo.ProjectionUnJit * ubo.ModelView;
        ubo.PrevViewProjection = ubo.ViewProjection;
        ubo.PrevViewProjectionUnJit = ubo.ViewProjectionUnJit;
        ubo.Jitter = glm::vec4(0.0f);
        ubo.ViewportRect = glm::vec4(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height));

        const Assets::EnvironmentSetting& env = scene.GetEnvSettings();
        const bool hasSun = env.HasSun && env.SunIntensity > 0.0f;
        ubo.SunDirection = glm::vec4(env.SunDirection(), 0.0f);
        ubo.SunColor = hasSun ? glm::vec4(1.0f, 1.0f, 1.0f, 0.0f) * env.SunIntensity : glm::vec4(0.0f);
        ubo.HasSun = hasSun;
        ubo.SkyRotation = env.SkyRotation;
        ubo.SkyIntensity = env.SkyIntensity;
        ubo.SkyIdx = static_cast<uint32_t>(std::max(env.SkyIdx, 0));
        ubo.HasSky = env.HasSky;
        ubo.LightCount = scene.GetLightCount();

        const auto cascades = env.ComputeSunCascades(
            ubo.ViewProjectionUnJit, camera.NearPlane, camera.FarPlane, 20.0f);
        for (uint32_t i = 0; i < Assets::Scene::kSunShadowCascadeCount; ++i)
        {
            ubo.SunCascadeViewProjection[i] = cascades.viewProjection[i];
        }
        ubo.CascadeSplits = cascades.splits;

        ubo.Aperture = camera.Aperture;
        ubo.FocusDistance = camera.FocalDistance;
        ubo.FastGather = false;
        ubo.SuperResolution = 0;
        ubo.MaxNumberOfBounces = 1;
        ubo.NumberOfSamples = 64;
        ubo.NumberOfBounces = 2;
        ubo.TemporalFrames = 1;
        ubo.TotalFrames = static_cast<uint32_t>(std::max(frame_.frameCount, 1));
        ubo.TAA = false;
        ubo.ProgressiveRender = false;
        ubo.HDR = false;
        ubo.HDROutputMode = 0;
        ubo.PaperWhiteNit = 200.0f;
        ubo.SceneEpsilonScale = 1.0f;
        ubo.HeatmapScale = 1.0f;
        ubo.ShowHeatmap = false;
        ubo.DebugDraw_Lighting = false;
        ubo.DebugDraw_ShadowCascadeCoverage = false;

        ubo.DenoiseDiffuseSourceSlot = static_cast<uint32_t>(Assets::Bindless::RT_ACCUMLATE_DIFFUSE);
        ubo.DenoiseSpecularSourceSlot = static_cast<uint32_t>(Assets::Bindless::RT_ACCUMLATE_SPECULAR);
        ubo.GTAORadius = 1.0f;
        ubo.GTAOStrength = 0.0f;
        ubo.GTAOThickness = 0.1f;
        ubo.GTAODebugMode = 0;
        ubo.GTAOEnable = false;
        ubo.GTAOQuality = 0;
        ubo.SkyVisEnable = false;
        ubo.SkyVisStrength = 0.0f;
        ubo.SkyVisMaxDistance = 1.0f;
        ubo.SkyVisRayCount = 1;
        ubo.SkyVisCombineMode = 0;
        ubo.SkyVisBlurRadius = 0;
        ubo.SkyVisJitterRadius = 0.0f;
        return ubo;
    }

    void VulkanBaseRenderer::CopyThumbnailViewOutput(
        VkCommandBuffer commandBuffer,
        RenderView& view,
        RenderImage& dst)
    {
        const RenderImage* src = GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_DENOISED);
        if (!src)
        {
            return;
        }

        const VkExtent2D extent = view.RenderExtent();
        src->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        dst.InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                          VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[0] = {0, 0, 0};
        region.srcOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[0] = {0, 0, 0};
        region.dstOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
        vkCmdBlitImage(commandBuffer,
                       src->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst.GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region, VK_FILTER_LINEAR);

        dst.InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        src->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    bool VulkanBaseRenderer::ScheduleNextMaterialThumbnail(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (pendingMaterialThumbnails_.empty())
        {
            return false;
        }

        auto mainScene = scene_.lock();
        if (!mainScene)
        {
            pendingMaterialThumbnails_.clear();
            return false;
        }

        const uint32_t materialIndex = pendingMaterialThumbnails_.front();
        pendingMaterialThumbnails_.erase(pendingMaterialThumbnails_.begin());
        if (materialIndex >= mainScene->Materials().size() || materialIndex >= kMaterialThumbnailMaxSlots)
        {
            return false;
        }

        if (!materialThumbnailSceneReady_)
        {
            return false;
        }
        if (!materialThumbnailScene_ || materialThumbnailScene_->Materials().empty())
        {
            return false;
        }

        EnsureThumbnailRenderTarget();
        assert(thumbnailRenderView_ != nullptr);

        const uint32_t sampleSlot = kMaterialThumbnailSampleSlotBase + materialIndex;

        if (materialThumbnailImages_.size() <= materialIndex)
        {
            materialThumbnailImages_.resize(static_cast<size_t>(materialIndex) + 1);
        }
        if (!materialThumbnailImages_[materialIndex])
        {
            const std::string debugName = fmt::format("Material Thumbnail {}", materialIndex);
            materialThumbnailImages_[materialIndex].reset(new RenderImage(
                Device(), kThumbnailExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                false, debugName.c_str()));
            ctx_.globalTexturePool->BindSampleTexture(
                sampleSlot, materialThumbnailImages_[materialIndex]->GetImageView(), *thumbnailSampler_);
        }

        materialThumbnailScene_->Materials()[0] = mainScene->Materials()[materialIndex];
        materialThumbnailScene_->Materials()[0].name_ = "__material_thumbnail_preview";

        const auto rendererIt = logicRenderers_.renderers.find(ERT_SoftwareModernNoAmbient);
        const auto fallbackIt = logicRenderers_.renderers.find(logicRenderers_.current);
        if (rendererIt == logicRenderers_.renderers.end() && fallbackIt == logicRenderers_.renderers.end())
        {
            return false;
        }

        materialThumbnailScene_->UpdateAllMaterials();
        materialThumbnailScene_->UpdateNodes();
        const Assets::UniformBufferObject previewCamera = BuildThumbnailUbo(*materialThumbnailScene_, kThumbnailExtent);

        thumbnailRenderView_->SetDebugName("material thumbnail view");
        thumbnailRenderView_->SetRenderExtent(kThumbnailExtent);
        SetRenderViewUbo(*thumbnailRenderView_, imageIndex, previewCamera);
        thumbnailRenderView_->SetVisibilityFramebuffer(thumbnailVisibilityFrameBuffer_.get());
        thumbnailRenderView_->SetSceneOverride(materialThumbnailScene_.get());
        thumbnailRenderView_->SetPrevDepthValid(false);
        ScheduleRenderView(
            *thumbnailRenderView_,
            *(rendererIt != logicRenderers_.renderers.end() ? rendererIt : fallbackIt)->second,
            /*clearSwapchain*/ false,
            [this, commandBuffer, materialIndex](RenderView& view)
            {
                if (materialIndex < materialThumbnailImages_.size() && materialThumbnailImages_[materialIndex])
                {
                    CopyThumbnailViewOutput(commandBuffer, view, *materialThumbnailImages_[materialIndex]);
                }
                view.SetSceneOverride(nullptr);
            });

        return true;
    }

    bool VulkanBaseRenderer::ScheduleNextMeshThumbnail(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (pendingMeshThumbnails_.empty())
        {
            return false;
        }

        auto mainScene = scene_.lock();
        if (!mainScene)
        {
            pendingMeshThumbnails_.clear();
            return false;
        }

        const uint32_t modelIndex = pendingMeshThumbnails_.front();
        pendingMeshThumbnails_.erase(pendingMeshThumbnails_.begin());
        if (modelIndex >= mainScene->Models().size() || modelIndex >= kMeshThumbnailMaxSlots)
        {
            return false;
        }

        const Assets::Model& model = mainScene->Models()[modelIndex];
        if (model.NumberOfVertices() == 0)
        {
            return false;
        }

        EnsureThumbnailRenderTarget();
        assert(thumbnailRenderView_ != nullptr);
        RebuildMeshThumbnailScene(model);
        if (!meshThumbnailScene_)
        {
            return false;
        }

        const uint32_t sampleSlot = kMeshThumbnailSampleSlotBase + modelIndex;

        if (meshThumbnailImages_.size() <= modelIndex)
        {
            meshThumbnailImages_.resize(static_cast<size_t>(modelIndex) + 1);
        }
        if (!meshThumbnailImages_[modelIndex])
        {
            const std::string debugName = fmt::format("Mesh Thumbnail {}", modelIndex);
            meshThumbnailImages_[modelIndex].reset(new RenderImage(
                Device(), kThumbnailExtent, VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                false, debugName.c_str()));
            ctx_.globalTexturePool->BindSampleTexture(
                sampleSlot, meshThumbnailImages_[modelIndex]->GetImageView(), *thumbnailSampler_);
        }

        const auto rendererIt = logicRenderers_.renderers.find(ERT_SoftwareModernNoAmbient);
        const auto fallbackIt = logicRenderers_.renderers.find(logicRenderers_.current);
        if (rendererIt == logicRenderers_.renderers.end() && fallbackIt == logicRenderers_.renderers.end())
        {
            return false;
        }

        meshThumbnailScene_->UpdateAllMaterials();
        meshThumbnailScene_->UpdateNodes();
        const Assets::UniformBufferObject previewCamera = BuildThumbnailUbo(*meshThumbnailScene_, kThumbnailExtent);

        thumbnailRenderView_->SetDebugName("mesh thumbnail view");
        thumbnailRenderView_->SetRenderExtent(kThumbnailExtent);
        SetRenderViewUbo(*thumbnailRenderView_, imageIndex, previewCamera);
        thumbnailRenderView_->SetVisibilityFramebuffer(thumbnailVisibilityFrameBuffer_.get());
        thumbnailRenderView_->SetSceneOverride(meshThumbnailScene_.get());
        thumbnailRenderView_->SetPrevDepthValid(false);
        ScheduleRenderView(
            *thumbnailRenderView_,
            *(rendererIt != logicRenderers_.renderers.end() ? rendererIt : fallbackIt)->second,
            /*clearSwapchain*/ false,
            [this, commandBuffer, modelIndex](RenderView& view)
            {
                if (modelIndex < meshThumbnailImages_.size() && meshThumbnailImages_[modelIndex])
                {
                    CopyThumbnailViewOutput(commandBuffer, view, *meshThumbnailImages_[modelIndex]);
                }
                view.SetSceneOverride(nullptr);
            });

        return true;
    }

    void VulkanBaseRenderer::CopySecondaryViewOutput(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        RenderView& view,
        uint32_t viewIndex)
    {
        viewIndex = std::min(viewIndex, kMaxSecondaryViews - 1);
        auto& resources = secondaryViews_[viewIndex];
        const RenderImage* src = GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_DENOISED);
        if (!src)
        {
            return;
        }

        const VkExtent2D secondaryExtent = view.RenderExtent();
        const int32_t rw = static_cast<int32_t>(secondaryExtent.width);
        const int32_t rh = static_cast<int32_t>(secondaryExtent.height);

        // Secondary view composed output -> transfer source.
        src->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        // 1) Copy into the offscreen sampled image so the editor can ImGui::Image it.
        if (resources.offscreenImage)
        {
            resources.offscreenImage->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageCopy copyRegion{};
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.extent = {static_cast<uint32_t>(rw), static_cast<uint32_t>(rh), 1};
            vkCmdCopyImage(commandBuffer,
                src->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                resources.offscreenImage->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &copyRegion);
            resources.offscreenImage->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // 2) Env demo: also composite as a picture-in-picture in the swapchain's bottom-right quarter
        //    (so `gnb shot` can verify without the editor UI).
        if (multiViewDemo_)
        {
            const int32_t sw = static_cast<int32_t>(frame_.swapChain->Extent().width);
            const int32_t sh = static_cast<int32_t>(frame_.swapChain->Extent().height);
            ImageMemoryBarrier::FullInsert(commandBuffer, frame_.swapChain->Images()[imageIndex],
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            VkImageBlit region{};
            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.srcOffsets[0] = {0, 0, 0};
            region.srcOffsets[1] = {rw, rh, 1};
            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.dstOffsets[0] = {sw / 2, sh / 2, 0};
            region.dstOffsets[1] = {sw, sh, 1};
            vkCmdBlitImage(commandBuffer,
                           src->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           frame_.swapChain->Images()[imageIndex], VK_IMAGE_LAYOUT_GENERAL,
                           1, &region, VK_FILTER_LINEAR);
        }

        // Restore denoised output to GENERAL for next frame's barrier initialization.
        src->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void VulkanBaseRenderer::ScheduleAuxiliaryViews(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (ScheduleNextMaterialThumbnail(commandBuffer, imageIndex))
        {
            return;
        }
        if (ScheduleNextMeshThumbnail(commandBuffer, imageIndex))
        {
            return;
        }

        const auto it = logicRenderers_.renderers.find(logicRenderers_.current);
        if (it == logicRenderers_.renderers.end())
        {
            return;
        }

        for (uint32_t viewIndex = 0; viewIndex < kMaxSecondaryViews; ++viewIndex)
        {
            auto& resources = secondaryViews_[viewIndex];
            if (!multiViewDemo_ && !resources.enabled && !resources.requested)
            {
                continue;
            }

            RenderView& secondaryView = EnsureSecondaryRenderView(viewIndex);
            const VkExtent2D secondaryExtent = secondaryView.RenderExtent();

            Assets::UniformBufferObject secondaryUbo = resources.cameraOverride.has_value()
                ? MakeCameraUbo(frame_.lastUBO, *resources.cameraOverride,
                                GetScene().GetEnvSettings(), secondaryExtent)
                : MakeOrbitedCameraUbo(frame_.lastUBO, GetScene().GetRenderCamera(),
                                       GetScene().GetEnvSettings(), secondaryExtent,
                                       35.0f + static_cast<float>(viewIndex) * 35.0f, 30.0f);
            FinalizeTemporalUbo(secondaryView, secondaryUbo);
            SetRenderViewUbo(secondaryView, imageIndex, secondaryUbo);
            secondaryView.SetVisibilityFramebuffer(resources.visibilityFrameBuffer.get());
            secondaryView.SetSceneOverride(nullptr);
            secondaryView.SetCopyObjectIdHistory(true);

            ScheduleRenderView(
                secondaryView,
                *it->second,
                /*clearSwapchain*/ false,
                [this, commandBuffer, imageIndex, viewIndex](RenderView& view)
                {
                    CopySecondaryViewOutput(commandBuffer, imageIndex, view, viewIndex);
                });
        }
    }

    void VulkanBaseRenderer::PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();

        if (CurrentRendererRequirements().requestAmbientCube && !ShouldSkipAmbientCubeUpdates())
        {
            if (settings.BakeSpeedLevel != 2)
            {
                const bool useHardware = caps_.supportRayTracing && !GOption->ForceSoftGen;
                BakeAmbientCubeCascade(commandBuffer, imageIndex, useHardware);
            }
        }

        // Voxel sky-visibility bake runs for voxel-only paths (SwModernNoAmbient): no cube pool, just
        // the voxel SDF. Gated separately so it does not depend on requestAmbientCube.
        if (CurrentRendererRequirements().requestVoxelGeometry &&
            !CurrentRendererRequirements().requestAmbientCube && !ShouldSkipAmbientCubeUpdates())
        {
            BakeVoxelSkyVisibility(commandBuffer, imageIndex);
        }

        DispatchVisualDebugger(commandBuffer, imageIndex);
    }

    void VulkanBaseRenderer::UpdateUniformBuffer(const uint32_t imageIndex)
    {
        frame_.lastUBO = GetUniformBufferObject(frame_.swapChain->RenderOffset(), frame_.swapChain->OutputExtent());
        frame_.uniformBuffers[imageIndex].SetValue(frame_.lastUBO);
        SetRenderViewUbo(PrimaryView(), imageIndex, frame_.lastUBO);
    }

    void VulkanBaseRenderer::CaptureFrameGenerationHudless(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        if (imageIndex >= frameGeneration_.hudlessImages.size())
        {
            return;
        }

        const VkImage swapchainImage = frame_.swapChain->Images()[imageIndex];
        auto& hudlessImage = *frameGeneration_.hudlessImages[imageIndex];

        ImageMemoryBarrier::FullInsert(
            commandBuffer,
            swapchainImage,
            0,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        hudlessImage.InsertBarrier(
            commandBuffer,
            0,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.extent = {
            frame_.swapChain->OutputExtent().width,
            frame_.swapChain->OutputExtent().height,
            1};
        vkCmdCopyImage(
            commandBuffer,
            swapchainImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            hudlessImage.GetImage().Handle(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &copyRegion);

        hudlessImage.InsertBarrier(
            commandBuffer,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL);
        ImageMemoryBarrier::FullInsert(
            commandBuffer,
            swapchainImage,
            VK_ACCESS_TRANSFER_READ_BIT,
            0,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }

    Rendering::Upscaler::FFrameInputs VulkanBaseRenderer::BuildUpscalerFrameInputs(
        VkCommandBuffer commandBuffer,
        uint32_t imageIndex,
        VkImageLayout swapchainLayout)
    {
        Rendering::Upscaler::FFrameInputs inputs{};
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();
        const auto& swapChain = SwapChain();

        inputs.commandBuffer = commandBuffer;
        inputs.frameToken = frame_.streamlineFrameToken;
        inputs.frameIndex = static_cast<uint32_t>(frame_.frameCount);
        inputs.imageIndex = imageIndex;
        inputs.reset = resetUpscalerHistory_ || frame_.frameCount < 2;
        inputs.enableDLSS = caps_.supportDLSS && settings.DLSS;
        inputs.enableDLSSRR = caps_.supportDLSSRR && settings.DLSSRR;
        inputs.enableDLSSG = caps_.supportDLSSG && settings.DLSSG;
        inputs.superResolutionMode = settings.SuperResolution;
        inputs.frameGenerationMultiplier = std::clamp(settings.DLSSGFrameMultiplier, 2u, 4u);
        inputs.hdrOutput = swapChain.IsHDR();
        inputs.renderExtent = swapChain.RenderExtent();
        inputs.outputExtent = swapChain.OutputExtent();
        inputs.outputOffset = swapChain.OutputOffset();
        inputs.swapchainFormat = swapChain.Format();
        inputs.backBufferCount = static_cast<uint32_t>(swapChain.Images().size());
        inputs.ubo = &frame_.lastUBO;

        auto& camera = GetScene().GetRenderCamera();
        inputs.camera.nearPlane = camera.NearPlane;
        inputs.camera.farPlane = camera.FarPlane;
        inputs.camera.verticalFovRadians = glm::radians(camera.FieldOfView);
        inputs.camera.aspectRatio =
            static_cast<float>(std::max(1u, swapChain.Extent().width)) /
            static_cast<float>(std::max(1u, swapChain.Extent().height));

        inputs.depth = MakeDepthResource(
            DepthBuffer(),
            swapChain.RenderExtent(),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        inputs.motionVectors = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_MOTIONVECTOR),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT);
        inputs.scalingInputColor = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_DENOISED),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        inputs.scalingOutputColor = MakeSwapchainResource(
            swapChain,
            imageIndex,
            swapChain.OutputExtent(),
            swapchainLayout);
        inputs.hudlessColor = inputs.scalingOutputColor;

        inputs.albedo = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_ALBEDO),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT);
        inputs.specularAlbedo = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_SPECULAR_ALBEDO),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT);
        inputs.normalRoughness = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_NORMAL),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT);
        inputs.diffuseNoisy = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        inputs.specularNoisy = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        inputs.diffuseHitDistance = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_DIFFUSE_HITDIST),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT);
        inputs.specularHitDistance = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_SPECULAR_HITDIST),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT);

        return inputs;
    }

    void VulkanBaseRenderer::OnPreLoadScene()
    {
        if (caps_.supportRayTracing)
        {
            DeleteAccelerationStructures();
        }
    }

    void VulkanBaseRenderer::OnPostLoadScene()
    {
        skin_.updateRequests.clear();
        if (caps_.supportRayTracing)
        {
            CreateAccelerationStructures();
        }
    }

    void VulkanBaseRenderer::AfterUpdateScene()
    {
        if (!caps_.supportRayTracing)
        {
            return;
        }

        auto& scene = GetScene();

        std::vector<VkAccelerationStructureInstanceKHR> instances;
        auto& nodeTrans = scene.GetNodeProxys();
        if (nodeTrans.empty() || rt_->blas.empty())
        {
            rt_->tlasUpdateRequest = 0;
            return;
        }

        instances.reserve(nodeTrans.size());
        for (size_t i = 0; i < nodeTrans.size(); i++)
        {
            auto& node = nodeTrans[i];
            const size_t blasIndex = node.modelId / 10;
            if (blasIndex >= rt_->blas.size())
            {
                SPDLOG_WARN("Skipping TLAS instance with stale model index {} (BLAS count {})", blasIndex,
                            rt_->blas.size());
                continue;
            }
            instances.push_back(RayTracing::TopLevelAccelerationStructure::CreateInstance(
                rt_->blas[blasIndex], glm::transpose(node.worldTS), node.instanceId, node.visible && !node.nort));
        }

        int instanceCount = static_cast<int>(instances.size());
        if (instanceCount > 0)
        {
            auto* data = reinterpret_cast<VkAccelerationStructureInstanceKHR*>(
                rt_->instancesMemory->Map(0, instances.size() * sizeof(VkAccelerationStructureInstanceKHR)));
            std::memcpy(data, instances.data(), instances.size() * sizeof(VkAccelerationStructureInstanceKHR));
            rt_->instancesMemory->Unmap();
        }

        rt_->tlasUpdateRequest = instanceCount;
    }

}
