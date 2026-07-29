#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Rendering/FrameSubmission.hpp"
#include "Engine/Rendering/RenderSubsystems.hpp"
#include "Engine/Vulkan/GpuQueryTimer.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Vulkan/RayTracing/RayTracingProperties.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/DeviceCreationAugmenter.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"
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
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Loaders/FProcModel.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Options.hpp"
#include "Engine/Rendering/SoftwareModern/SoftwareModernRenderer.hpp"
#include "Engine/Rendering/SoftwareModern/SoftwareModernNoAmbientRenderer.hpp"
#include "Engine/Rendering/SoftwareTracing/SoftwareTracingRenderer.hpp"
#include "Engine/Rendering/PathTracing/PathTracingRenderer.hpp"
#include "Engine/Rendering/ExternalPassRegistry.hpp"
#include "Engine/Rendering/Preview/RenderViewServices.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Rendering/VoxelTracing/VoxelTracingRenderer.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Editor/UserInterface.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/PipelineCommon/RestirDI.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include "Engine/Rendering/Atmosphere/AtmosphereSubsystem.hpp"
#include "Engine/Rendering/Upscaler/IUpscaler.hpp"
#include "Engine/Rendering/Upscaler/UpscalerRegistry.hpp"

#include <algorithm>
#include <limits>
#include <utility>

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
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
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
            swapChain.ImageUsage()};
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
            FRendererContract contract;
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
            RendererDescriptor{ERT_PathTracing, "PathTracing", {
                                   ESceneResource::Voxel | ESceneResource::Ambient | ESceneResource::TLAS | ESceneResource::SHARC,
                                   EViewPrepass::Cull | EViewPrepass::Clear | EViewPrepass::Visibility,
                                   ERenderOutput::Color | ERenderOutput::Depth | ERenderOutput::Motion | ERenderOutput::ObjectId |
                                       ERenderOutput::Normal | ERenderOutput::Albedo | ERenderOutput::Diffuse | ERenderOutput::Specular,
                                   EPostProcess::Temporal | EPostProcess::Upscale |
                                       EPostProcess::RayReconstruction | EPostProcess::FrameGeneration |
                                       EPostProcess::DebugGBuffer,
                                   EHistoryChannel::Diffuse | EHistoryChannel::Specular | EHistoryChannel::Albedo | EHistoryChannel::ObjectId}, 1, 1,
                               &CreateLogicRenderer<PathTracing::PathTracingRenderer>},
            RendererDescriptor{ERT_SoftwareTracing, "SoftwareTracing", {
                                   ESceneResource::Voxel | ESceneResource::Ambient,
                                   EViewPrepass::Cull | EViewPrepass::Clear | EViewPrepass::Visibility | EViewPrepass::CSM,
                                   ERenderOutput::Color | ERenderOutput::Depth | ERenderOutput::Motion | ERenderOutput::ObjectId |
                                       ERenderOutput::Normal | ERenderOutput::Albedo | ERenderOutput::Diffuse | ERenderOutput::Specular,
                                   EPostProcess::Temporal | EPostProcess::Upscale |
                                       EPostProcess::DebugGBuffer,
                                   EHistoryChannel::Diffuse | EHistoryChannel::Specular | EHistoryChannel::Albedo | EHistoryChannel::ObjectId}, 1, 0,
                               &CreateLogicRenderer<SoftwareTracing::SoftwareTracingRenderer>},
            RendererDescriptor{ERT_SoftwareModern, "SoftwareModern", {
                                   ESceneResource::Voxel | ESceneResource::Ambient,
                                   EViewPrepass::Cull | EViewPrepass::Clear | EViewPrepass::Visibility | EViewPrepass::CSM,
                                   ERenderOutput::Color | ERenderOutput::Depth | ERenderOutput::Motion | ERenderOutput::ObjectId |
                                       ERenderOutput::Normal | ERenderOutput::Albedo | ERenderOutput::Diffuse | ERenderOutput::Specular,
                                   EPostProcess::Temporal | EPostProcess::Upscale |
                                       EPostProcess::DebugGBuffer,
                                   EHistoryChannel::Diffuse | EHistoryChannel::Specular | EHistoryChannel::Albedo | EHistoryChannel::ObjectId}, 0, 0,
                               &CreateLogicRenderer<SoftwareModern::SoftwareModernRenderer>},
            RendererDescriptor{ERT_VoxelTracing, "VoxelTracing", {
                                   ESceneResource::Voxel | ESceneResource::Ambient,
                                   EViewPrepass::None,
                                   ERenderOutput::Color,
                                   EPostProcess::Upscale,
                                   EHistoryChannel::None, false}, 0, 1,
                               &CreateLogicRenderer<VoxelTracing::VoxelTracingRenderer>},
            RendererDescriptor{ERT_SoftwareModernNoAmbient, "SoftwareModernNoAmbient", {
                                   ESceneResource::None,
                                   EViewPrepass::Cull | EViewPrepass::Clear | EViewPrepass::Visibility | EViewPrepass::CSM,
                                   ERenderOutput::Color | ERenderOutput::Depth | ERenderOutput::Motion |
                                       ERenderOutput::ObjectId | ERenderOutput::Normal,
                                   EPostProcess::Upscale | EPostProcess::DebugGBuffer,
                                   EHistoryChannel::None, true}, 0, 1,
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
        const FRendererContract& contract = GetRendererDescriptor(type).contract;
        return {
            .requestAmbientCube = HasAny(contract.sceneResources, ESceneResource::Ambient),
            .requestRayTracing = HasAny(contract.sceneResources, ESceneResource::TLAS),
            .requestVoxelGeometry = HasAny(contract.sceneResources, ESceneResource::Voxel),
        };
    }

    const FRendererContract& GetRendererContract(const ERendererType type)
    {
        return GetRendererDescriptor(type).contract;
    }

    const char* GetRendererName(ERendererType type)
    {
        return GetRendererDescriptor(type).name;
    }

    const std::array<ERendererType, 4>& GetReferenceRendererTypes()
    {
        static constexpr std::array<ERendererType, 4> kReferenceRendererTypes{
            ERT_SoftwareModern,
            ERT_SoftwareTracing,
            ERT_SoftwareModernNoAmbient,
            ERT_PathTracing,
        };
        return kReferenceRendererTypes;
    }

    FReferenceViewLayout GetReferenceViewLayout(const ERendererType type)
    {
        const RendererDescriptor& descriptor = GetRendererDescriptor(type);
        return {
            .debugName = descriptor.name,
            .column = descriptor.referenceColumn,
            .row = descriptor.referenceRow,
        };
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
        forceSDR_ = GOption->ForceSDR;

        caps_.supportRayTracing = false;
        upscaler_ = Rendering::Upscaler::CreateRegisteredUpscaler();
        renderViewServices_ = std::make_unique<RenderViewServices>(*this);
        rayTracingSceneBackend_ = std::make_unique<RayTracingSceneBackend>(*this);
        ambientCubeBaker_ = std::make_unique<AmbientCubeBaker>(*this);
        gpuDrivenPasses_ = std::make_unique<GpuDrivenPasses>(*this);
        atmosphere_ = std::make_unique<Rendering::Atmosphere::AtmosphereSubsystem>(*this);
    }

    VulkanBaseRenderer::~VulkanBaseRenderer()
    {
        VulkanBaseRenderer::DeleteSwapChain();
        DeleteAccelerationStructures();
        atmosphere_.reset();
        // Volume resources outlive the swapchain, so they are released here rather than in
        // DeleteSwapChain -- but still before the device goes away.
        bindless_.volumeImages.clear();
        rt_.reset();
        renderViewServices_.reset();
        logicRenderers_.renderers.clear();
        logicRenderers_.swapChainCreatedTypes.clear();
        restirDI_.reset();
        renderViews_.reset();
        upscaler_.reset();
        ctx_.frameProfiler.reset();
        ctx_.globalTexturePool.reset();
        ctx_.commandPool.reset();
        ctx_.commandPool2.reset();
        ctx_.device.reset();
        ctx_.surface.reset();
        ctx_.debugUtilsMessenger.reset();
        ctx_.instance.reset();
        ctx_.window = nullptr;
    }

    PipelineCommon::RestirDI& VulkanBaseRenderer::RestirDIResources()
    {
        if (!restirDI_)
        {
            restirDI_ = std::make_unique<PipelineCommon::RestirDI>(*this);
        }
        return *restirDI_;
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
        uint32_t cascadeCount = Assets::CUBE_CASCADE_MAX;
        float poolBrickRatio = 1.0f;
        if (NextEngine::GetInstance())
        {
            const auto& settings = NextEngine::GetInstance()->GetUserSettings();
            cascadeCount = Assets::SanitizeAmbientCubeCascadeCount(settings.AmbientCubeCascadeCount);
            poolBrickRatio = settings.AmbientCubePoolBrickRatio;
        }
        const float clampedPoolBrickRatio = std::clamp(poolBrickRatio, 0.0f, 1.0f);
        const auto poolBricksPerCascade = static_cast<VkDeviceSize>(std::max(
            1.0f, std::ceil(static_cast<float>(Assets::GPU_SCENE_AMBIENT_BRICKS_PER_CASCADE) * clampedPoolBrickRatio)));
        const VkDeviceSize poolCubesPerCascade =
            poolBricksPerCascade * static_cast<VkDeviceSize>(Assets::GPU_SCENE_AMBIENT_BRICK_VOLUME);

        const VkDeviceSize fullAmbientCubeAllocationSize =
            static_cast<VkDeviceSize>(cascadeCount) *
                (perCascadeCount * sizeof(Assets::VoxelData) + poolCubesPerCascade * sizeof(Assets::AmbientCube)) +
            static_cast<VkDeviceSize>(Assets::ACGI_PAGE_COUNT) * Assets::ACGI_PAGE_COUNT * sizeof(Assets::PageIndex) +
            poolCubesPerCascade * sizeof(Assets::AmbientCube) +
            perCascadeCount * sizeof(glm::u32vec4) +
            perCascadeCount * sizeof(glm::u32vec4);
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

        ERendererType resolvedRendererType = logicRenderers_.current;
        if (!caps_.supportRayTracing && GetRendererRequirements(resolvedRendererType).requestRayTracing)
        {
            resolvedRendererType = ERT_SoftwareTracing;
        }
        if (!caps_.fullAmbientCubeBudget && GetRendererRequirements(resolvedRendererType).requestAmbientCube)
        {
            resolvedRendererType = ERT_SoftwareModernNoAmbient;
        }
        if (resolvedRendererType != logicRenderers_.current)
        {
            SwitchLogicRenderer(resolvedRendererType);
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
        atmosphere_->CreateDeviceResources();
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
        DeleteSwapChain();
        DeleteAccelerationStructures();
        bindless_.volumeImages.clear();
        if (upscaler_)
        {
            upscaler_->Shutdown();
        }
        ctx_.frameProfiler.reset();
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
        if (activeViewContext_.sceneOverride != nullptr)
        {
            return *activeViewContext_.sceneOverride;
        }
        auto scene = sceneState_.scene.lock();
        if (!scene)
        {
            SPDLOG_CRITICAL("VulkanBaseRenderer attempted to access an expired scene (frame {}, renderer {})",
                            frame_.frameCount, GetRendererName(logicRenderers_.current));
            Throw(std::runtime_error("renderer scene lifetime expired"));
        }
        return *scene;
    }

    void VulkanBaseRenderer::SetScene(std::shared_ptr<Assets::Scene> scene)
    {
        sceneState_.scene = scene;
        ++sceneState_.generation;
        renderViews_->InvalidateAllTemporalHistory(EHistoryInvalidationReason::SceneChanged);
        if (renderViewServices_)
        {
            renderViewServices_->OnMainSceneChanged();
        }
        RequestClearAmbientCubeCache();
        resetUpscalerHistory_ = true;
    }

    void VulkanBaseRenderer::OnHdrShUpdated()
    {
        if (renderViewServices_)
        {
            renderViewServices_->OnHdrShUpdated();
        }
    }

    Rendering::Upscaler::FFrameGenerationState VulkanBaseRenderer::GetFrameGenerationState() const
    {
        return upscaler_ ? upscaler_->FrameGenerationState() : Rendering::Upscaler::FFrameGenerationState{};
    }

    uint32_t VulkanBaseRenderer::TemporalJitterFrameCount() const
    {
        return upscaler_ ? upscaler_->JitterPhaseCount() : 0;
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
        if (activeViewContext_.renderExtent.width > 0 && activeViewContext_.renderExtent.height > 0)
        {
            return activeViewContext_.renderExtent;
        }
        return frame_.swapChain->RenderExtent();
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
            // Slang currently emits an unused SPV_KHR_ray_tracing declaration for
            // RayQuery shaders. Enabling the extension satisfies module validation;
            // rayTracingPipeline remains disabled and the renderer uses RayQuery only.
            if (HasDeviceExtension(physicalDevice, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME))
            {
                requiredExtensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
            }
            accelerationStructureFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            accelerationStructureFeatures.pNext = nextDeviceFeatures;
            accelerationStructureFeatures.accelerationStructure = true;
            rayQueryFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
            rayQueryFeatures.pNext = &accelerationStructureFeatures;
            rayQueryFeatures.rayQuery = true;
            nextDeviceFeatures = &rayQueryFeatures;
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

        // Module-requested device extensions / features (NextRemote video encode,
        // upscaler backends, ...). Augmenters own any feature structs they chain in
        // and can inspect the chain built so far to avoid duplicate entries.
        for (IDeviceCreationAugmenter* augmenter : DeviceCreationAugmenters())
        {
            nextDeviceFeatures = augmenter->OnPhysicalDeviceSelected(
                ctx_.instance->Handle(), physicalDevice, requiredExtensions, nextDeviceFeatures);
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

        VkPhysicalDeviceScalarBlockLayoutFeatures supportedScalarBlockLayoutFeatures = {};
        supportedScalarBlockLayoutFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
        supportedScalarBlockLayoutFeatures.pNext = &supportedStorage16BitFeatures;

        VkPhysicalDeviceFeatures2 supportedFeatures2 = {};
        supportedFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supportedFeatures2.pNext = &supportedScalarBlockLayoutFeatures;
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

        VkPhysicalDeviceScalarBlockLayoutFeatures scalarBlockLayoutFeatures = {};
        scalarBlockLayoutFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES;
        scalarBlockLayoutFeatures.pNext = &storage16BitFeatures;
        scalarBlockLayoutFeatures.scalarBlockLayout = supportedScalarBlockLayoutFeatures.scalarBlockLayout;

        ctx_.device.reset(new class Device(physicalDevice, *ctx_.surface, requiredExtensions, deviceFeatures,
                                       &scalarBlockLayoutFeatures));
        ctx_.commandPool.reset(new class CommandPool(*ctx_.device, ctx_.device->GraphicsFamilyIndex(), 0, true));
        ctx_.commandPool2.reset(new class CommandPool(*ctx_.device, ctx_.device->TransferFamilyIndex(), 1, true));
        ctx_.frameProfiler = std::make_unique<Runtime::FrameProfiler>(
            std::make_unique<Vulkan::GpuQueryTimer>(*ctx_.device, 200, ctx_.device->DeviceProperties()));
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

            // The upscaler module fills in its device caps (already masked by whether
            // its required device extensions got enabled during device creation).
            Rendering::Upscaler::FFeatureCaps featureCaps{};
            upscaler_->OnDeviceCreated(deviceInfo, featureCaps);
            caps_.supportedUpscalerTypes = featureCaps.supportedTypes;
            caps_.frameGenerationTypes = featureCaps.frameGenerationTypes;
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

    namespace
    {
        // 3D storage/sampled image support varies by driver and format, so probe before creating
        // rather than letting vkCreateImage fail with a bare VK_ERROR_FORMAT_NOT_SUPPORTED.
        void ValidateVolumeFormatSupport(const Device& device, VkExtent3D extent, VkFormat format,
                                         VkImageTiling tiling, VkImageUsageFlags usage, const char* debugName)
        {
            const char* name = debugName ? debugName : "<unnamed>";

            VkFormatProperties formatProperties{};
            vkGetPhysicalDeviceFormatProperties(device.PhysicalDevice(), format, &formatProperties);
            const VkFormatFeatureFlags features = tiling == VK_IMAGE_TILING_LINEAR
                ? formatProperties.linearTilingFeatures
                : formatProperties.optimalTilingFeatures;

            VkFormatFeatureFlags requiredFeatures = 0;
            if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
            {
                requiredFeatures |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
            }
            if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
            {
                requiredFeatures |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            }
            if ((features & requiredFeatures) != requiredFeatures)
            {
                Throw(std::runtime_error(fmt::format(
                    "3D image '{}' unsupported: format {} tiling {} lacks required features (have 0x{:x}, need 0x{:x})",
                    name, static_cast<int>(format), static_cast<int>(tiling), features, requiredFeatures)));
            }

            VkImageFormatProperties imageFormatProperties{};
            const VkResult result = vkGetPhysicalDeviceImageFormatProperties(
                device.PhysicalDevice(), format, VK_IMAGE_TYPE_3D, tiling, usage, 0, &imageFormatProperties);
            if (result != VK_SUCCESS)
            {
                Throw(std::runtime_error(fmt::format(
                    "3D image '{}' unsupported: format {} tiling {} usage 0x{:x} rejected by the device (VkResult {})",
                    name, static_cast<int>(format), static_cast<int>(tiling), usage, static_cast<int>(result))));
            }

            if (extent.width > imageFormatProperties.maxExtent.width ||
                extent.height > imageFormatProperties.maxExtent.height ||
                extent.depth > imageFormatProperties.maxExtent.depth)
            {
                Throw(std::runtime_error(fmt::format(
                    "3D image '{}' too large: requested {}x{}x{}, device maximum {}x{}x{}",
                    name, extent.width, extent.height, extent.depth,
                    imageFormatProperties.maxExtent.width, imageFormatProperties.maxExtent.height,
                    imageFormatProperties.maxExtent.depth)));
            }
        }
    }

    void VulkanBaseRenderer::CreateStorageImage3D(uint32_t bindlessIdx, VkExtent3D extent, VkFormat format,
                                                  VkImageTiling tiling, VkImageUsageFlags usage,
                                                  const char* debugName, const SamplerConfig& samplerConfig)
    {
        if (extent.width == 0 || extent.height == 0 || extent.depth == 0)
        {
            Throw(std::invalid_argument(fmt::format(
                "3D image '{}' has a zero extent ({}x{}x{})", debugName ? debugName : "<unnamed>",
                extent.width, extent.height, extent.depth)));
        }
        ValidateVolumeFormatSupport(Device(), extent, format, tiling, usage, debugName);

        auto image = std::make_unique<RenderImage>(Device(), extent, VK_IMAGE_TYPE_3D, format, tiling, usage,
                                                   debugName, samplerConfig);
        if (usage & VK_IMAGE_USAGE_STORAGE_BIT)
        {
            ctx_.globalTexturePool->BindStorageTexture(bindlessIdx, image->GetImageView());
        }
        if (usage & VK_IMAGE_USAGE_SAMPLED_BIT)
        {
            ctx_.globalTexturePool->BindSampleTexture(bindlessIdx, image->GetImageView(), image->Sampler());
        }
        bindless_.volumeImages[bindlessIdx] = std::move(image);
    }

    void VulkanBaseRenderer::DestroyStorageImage3D(uint32_t bindlessIdx)
    {
        bindless_.volumeImages.erase(bindlessIdx);
    }

    const RenderImage* VulkanBaseRenderer::GetStorageImage3D(uint32_t bindlessIdx) const
    {
        const auto it = bindless_.volumeImages.find(bindlessIdx);
        return it == bindless_.volumeImages.end() ? nullptr : it->second.get();
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

        CREATE_STORAGE_IMAGE(RT_SINGLE_DIFFUSE, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        CREATE_STORAGE_IMAGE(RT_MINIGBUFFER, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_MINIGBUFFER_DRAW, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT );
        CREATE_STORAGE_IMAGE(RT_OBJECTID_0, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        CREATE_STORAGE_IMAGE(RT_OBJECTID_1, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        // External temporal upscalers bind motion vectors as sampled images during their
        // current-frame dispatch. Keep STORAGE for engine writes and SAMPLED for the SDK read.
        CREATE_STORAGE_IMAGE(RT_MOTIONVECTOR, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
        CREATE_STORAGE_IMAGE(RT_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_SHADER_TIMER, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_SCENE_COLOR, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT );
        CREATE_STORAGE_IMAGE(RT_PREV_DEPTHBUFFER, VK_FORMAT_R32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_SINGLE_SPECULAR, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        CREATE_STORAGE_IMAGE(RT_BSDF_DATA, VK_FORMAT_R32G32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_MOTIONMOMENT, VK_FORMAT_R16_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_DIFFUSE_HITDIST, VK_FORMAT_R16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_SPECULAR_HITDIST, VK_FORMAT_R16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_SPECULAR_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        CREATE_STORAGE_IMAGE(RT_SPLAT_ACCUM, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
        CREATE_STORAGE_IMAGE(RT_AMBIENT, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT);
        const VkExtent2D gtaoExtent{
            (extent.width + 1u) / 2u,
            (extent.height + 1u) / 2u};
        CreateStorageImage(bankBase + Assets::Bindless::RT_GTAO, gtaoExtent, VK_FORMAT_R16_SFLOAT,
                           VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, "RT_GTAO");
    }
#undef CREATE_STORAGE_IMAGE

    void VulkanBaseRenderer::EnsureProgressiveRenderTarget()
    {
        assert(ActiveViewBankBase() == 0);
        const uint32_t bindlessIdx = Assets::Bindless::RT_PROGRESSIVE_SCENE_COLOR;
        if (bindless_.images.size() <= bindlessIdx)
        {
            bindless_.images.resize(bindlessIdx + 1);
        }
        if (bindless_.images[bindlessIdx])
        {
            return;
        }

        const VkFormat progressiveHistoryFormat = GOption->HighPrecisionProgressiveHistory
            ? VK_FORMAT_R32G32B32A32_SFLOAT
            : VK_FORMAT_R16G16B16A16_SFLOAT;
        CreateStorageImage(bindlessIdx, ActiveViewRenderExtent(), progressiveHistoryFormat,
                           VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT,
                           "RT_PROGRESSIVE_SCENE_COLOR");
    }

    void VulkanBaseRenderer::CreateRenderImages()
    {
        screenshot_.image.reset(new Image(*ctx_.device, frame_.swapChain->Extent(), 1, frame_.swapChain->Format(),
                                         VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_TRANSFER_DST_BIT));
        screenshot_.imageMemory.reset(new DeviceMemory(
            screenshot_.image->
            AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)));
        screenshot_.captureRequested = false;
        screenshot_.captureReady = false;
        screenshot_.initialized = false;
        screenshot_.captureSubmitSerial = 0;
        ctx_.device->DebugUtils().SetObjectName(screenshot_.image->Handle(), "Screenshot Image");
        screenshot_.imageMemory->SetName("Screenshot Memory");

        bindless_.images.resize(Assets::Bindless::RT_COUNT);

        // Primary view RT bank (bank 0 == legacy absolute layout).
        CreateRenderTargetBank(0);
        renderViews_->ResetSwapChainResources();
        // Non-primary view resources were destroyed with the swapchain; recreate on demand.
        if (renderViewServices_)
        {
            renderViewServices_->OnSwapChainResourcesInvalidated(/*releaseOffscreenSampledOutputs*/ false);
        }
        for (uint32_t i = 0; i != frame_.swapChain->Images().size(); i++)
        {
            if (frame_.swapChain->SupportsUsage(VK_IMAGE_USAGE_STORAGE_BIT))
            {
                ctx_.globalTexturePool->BindStorageTexture(
                    Assets::Bindless::RT_SWAPCHAIN0 + i, *frame_.swapChain->ImageViews()[i]);
            }
            else
            {
                ctx_.globalTexturePool->BindStorageTexture(
                    Assets::Bindless::RT_SWAPCHAIN0 + i,
                    GetViewStorageImage(Assets::Bindless::RT_SCENE_COLOR)->GetImageView());
            }
        }

        lateToneMapping_.inputInitialized.clear();
        {
            const size_t imageCount = frame_.swapChain->Images().size();
            if (Assets::Bindless::RT_TONEMAP_INPUT0 + imageCount > Assets::Bindless::RT_REMOTE_ENCODE0_Y)
            {
                Throw(std::runtime_error("Late tone-mapping bindless slot range exhausted"));
            }
            lateToneMapping_.inputInitialized.assign(imageCount, false);
            for (size_t i = 0; i < imageCount; ++i)
            {
                const std::string debugName = fmt::format("Late tone-mapping input {}", i);
                bindless_.images[Assets::Bindless::RT_TONEMAP_INPUT0 + i].reset(new RenderImage(
                    Device(),
                    frame_.swapChain->OutputExtent(),
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    false,
                    debugName.c_str()));
                ctx_.globalTexturePool->BindStorageTexture(
                    Assets::Bindless::RT_TONEMAP_INPUT0 + static_cast<uint32_t>(i),
                    bindless_.images[Assets::Bindless::RT_TONEMAP_INPUT0 + i]->GetImageView());
            }
        }

        frameGeneration_.hudlessImages.clear();
        const bool frameGenerationActive = temporalSuperResolutionActive_ &&
            frameSettings_.userSettings.FrameGeneration &&
            SupportsFrameGeneration(activeUpscalerType_);
        if (frameGenerationActive)
        {
            frameGeneration_.hudlessImages.reserve(frame_.swapChain->Images().size());
            for (size_t i = 0; i < frame_.swapChain->Images().size(); ++i)
            {
                const std::string debugName = fmt::format("Frame Generation HUD-less {}", i);
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

        temporalPostFilter_.pingImages.clear();
        temporalPostFilter_.pongImages.clear();
        temporalPostFilter_.pingInitialized.clear();
        temporalPostFilter_.pongInitialized.clear();
        const bool temporalPostFilterActive = temporalSuperResolutionActive_ &&
            Rendering::Upscaler::GetUpscalerTypeInfo(
                static_cast<uint32_t>(activeUpscalerType_)).supportsTemporalPostFilter;
        if (temporalPostFilterActive)
        {
            const size_t imageCount = frame_.swapChain->Images().size();
            if (Assets::Bindless::RT_TEMPORAL_POST_PONG0 + imageCount > Assets::Bindless::RT_REMOTE_ENCODE0_Y)
            {
                Throw(std::runtime_error("Temporal post-filter bindless slot range exhausted"));
            }
            temporalPostFilter_.pingImages.reserve(imageCount);
            temporalPostFilter_.pongImages.reserve(imageCount);
            temporalPostFilter_.pingInitialized.assign(imageCount, false);
            temporalPostFilter_.pongInitialized.assign(imageCount, false);
            for (size_t i = 0; i < imageCount; ++i)
            {
                const std::string pingDebugName = fmt::format("Temporal post-filter ping {}", i);
                temporalPostFilter_.pingImages.emplace_back(std::make_unique<RenderImage>(
                    Device(),
                    frame_.swapChain->OutputExtent(),
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT,
                    false,
                    pingDebugName.c_str()));
                ctx_.globalTexturePool->BindStorageTexture(
                    Assets::Bindless::RT_TEMPORAL_POST_PING0 + static_cast<uint32_t>(i),
                    temporalPostFilter_.pingImages.back()->GetImageView());

                const std::string pongDebugName = fmt::format("Temporal post-filter pong {}", i);
                temporalPostFilter_.pongImages.emplace_back(std::make_unique<RenderImage>(
                    Device(),
                    frame_.swapChain->OutputExtent(),
                    VK_FORMAT_R16G16B16A16_SFLOAT,
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_STORAGE_BIT,
                    false,
                    pongDebugName.c_str()));
                ctx_.globalTexturePool->BindStorageTexture(
                    Assets::Bindless::RT_TEMPORAL_POST_PONG0 + static_cast<uint32_t>(i),
                    temporalPostFilter_.pongImages.back()->GetImageView());
            }
        }
    }

    void VulkanBaseRenderer::CreateSceneSwapChainResources()
    {
        atmosphere_->CreateSwapChainPipelines();
        overlay_.wireframePipeline.reset(new class PipelineCommon::GraphicsPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene(), true));
        overlay_.wireframeFrameBuffers.clear();
        overlay_.wireframeFrameBuffers.reserve(frame_.swapChain->ImageViews().size());
        for (size_t i = 0; i < frame_.swapChain->ImageViews().size(); ++i)
        {
            overlay_.wireframeFrameBuffers.emplace_back(
                frame_.swapChain->RenderExtent(),
                GetViewStorageImage(Assets::Bindless::RT_SCENE_COLOR)->GetImageView(),
                overlay_.wireframePipeline->RenderPass());
        }

        // Shared pipelines.
        const bool temporalPostFilterActive = temporalSuperResolutionActive_ &&
            Rendering::Upscaler::GetUpscalerTypeInfo(
                static_cast<uint32_t>(activeUpscalerType_)).supportsTemporalPostFilter;
        if (temporalPostFilterActive)
        {
            overlay_.temporalPostFilterPipeline.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(
                SwapChain(), "assets/shaders/Process.TemporalPostFilter.comp.slang.spv", 48));
        }
        overlay_.toneMappingPipeline.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(
            SwapChain(), "assets/shaders/Process.TonemapAfterUpscaler.comp.slang.spv", 52));
        overlay_.bufferClearPipeline.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(*frame_.swapChain, "assets/shaders/Util.BufferClear.comp.slang.spv", 4));
        // Shared swap-chain resources must cover every registered renderer because
        // switching logic renderers does not recreate the swap chain.
        if (RegisteredRendererRequirements().requestAmbientCube)
        {
            ambient_.softBake.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.SwAmbientCube.comp.slang.spv", GetScene()));
            ambient_.clearCache.reset( new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Bake.ClearAmbientCubeCache.comp.slang.spv", GetScene()));

            if (caps_.supportRayTracing)
            {
                rt_->directLightGenPipeline.reset(new PipelineCommon::ZeroBindWithTLASPipeline(
                    SwapChain(), "assets/shaders/Bake.HwAmbientCube.comp.slang.spv", GetScene(), ActiveTLASHandle()));
            }
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

        for (const FExternalPassFactory& factory : ExternalPassFactories())
        {
            overlay_.externalPasses.push_back(factory(*this));
            overlay_.externalPasses.back()->CreateResources();
        }

        overlay_.visibilityPipeline.reset(new PipelineCommon::VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));
        overlay_.visibilityFrameBuffer.reset(new FrameBuffer(frame_.swapChain->RenderExtent(), GetViewStorageImage(Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImageView(), overlay_.visibilityPipeline->RenderPass()));

        // Directional-sun CSM shadow pass; register four cascades with the bindless set.
        {
            overlay_.sunShadowPass.reset(new Shadow::ShadowMapPass(*ctx_.device));
            overlay_.sunShadowPass->CreateResources(GetScene());

            auto* texPool = Assets::GlobalTexturePool::GetInstance();
            for (uint32_t i = 0; i < Assets::Scene::kSunShadowCascadeCount; ++i)
            {
                texPool->BindShadowMap(i, GetScene().SunShadowImageView(i), GetScene().SunShadowSampler());
            }
        }

        if (LogicRendererBase* logicRenderer = EnsureLogicRenderer(logicRenderers_.current))
        {
            EnsureLogicRendererSwapChain(logicRenderers_.current, *logicRenderer);
        }
    }

    void VulkanBaseRenderer::CreateSwapChain()
    {
        // Wait for the window.
        while (ctx_.window->IsMinimized())
        {
            ctx_.window->WaitForEvents();
        }

        // SwapChain
        auto* engine = NextEngine::GetInstance();
        frameSettings_.userSettings = engine->GetUserSettings();
        frameSettings_.progressiveRendering = engine->IsProgressiveRendering();
        frameSettings_.offlineProgressivePathTracing = engine->IsOfflineProgressivePathTracing();
        frameSettings_.effectiveSharc = engine->IsEffectiveSharcEnabled();
        frameSettings_.progressiveAccumulatedFrames = engine->GetProgressiveRenderAccumulatedFrames();
        frameSettings_.progressiveTargetFrames = engine->GetProgressiveRenderTargetFrames();
        const auto& settings = frameSettings_.userSettings;
        const auto requestedUpscalerType = Rendering::Upscaler::GetUpscalerTypeInfo(
            static_cast<uint32_t>(settings.UpscalerType)).type;
        const auto& requestedTypeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
            static_cast<uint32_t>(requestedUpscalerType));
        const FRendererContract& requestedContract = GetRendererContract(logicRenderers_.current);
        const bool canActivateRequestedType = !GOption->ReferenceMode && upscaler_ &&
            requestedUpscalerType != Rendering::Upscaler::EUpscalerType::None &&
            SupportsUpscaler(requestedUpscalerType) &&
            (!requestedTypeInfo.requiresDepthAndMotion ||
             HasAll(requestedContract.outputs, ERenderOutput::Depth | ERenderOutput::Motion)) &&
            HasAny(requestedContract.post, EPostProcess::Upscale) &&
            (!requestedTypeInfo.requiresRayReconstruction ||
             HasAny(requestedContract.post, EPostProcess::RayReconstruction));
        frameGenerationSwapchainRequested_ = settings.FrameGeneration &&
            SupportsFrameGeneration(requestedUpscalerType) && canActivateRequestedType;
        const VkPresentModeKHR requestedPresentMode =
            frameGenerationSwapchainRequested_
                ? VK_PRESENT_MODE_IMMEDIATE_KHR
                : presentMode_;
        frame_.swapChain.reset(new class SwapChain(*ctx_.device, requestedPresentMode, forceSDR_));
        swapchainStateTracker_.Reset();
        auxiliaryImageStateTracker_.Reset();
        frame_.currentFrame = 0;
        frame_.currentImageIndex = 0;
        frame_.currentFence = nullptr;
        frame_.currentFenceSerial = 0;
        VkExtent2D renderExtent = frame_.swapChain->Extent();
        activeUpscalerType_ = Rendering::Upscaler::EUpscalerType::None;
        temporalSuperResolutionActive_ = false;
        effectiveSuperResolutionMode_ =
            static_cast<uint32_t>(Rendering::Upscaler::EUpscaleMode::Native);
        if (!GOption->ReferenceMode)
        {
            const auto resolvedMode = Rendering::Upscaler::ResolveUpscaleMode(
                settings.SuperResolution,
                frame_.swapChain->Extent());
            effectiveSuperResolutionMode_ = resolvedMode.mode;

            const FRendererContract& contract = GetRendererContract(logicRenderers_.current);
            const auto& typeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
                static_cast<uint32_t>(requestedUpscalerType));
            const bool hasRequiredOutputs = !typeInfo.requiresDepthAndMotion ||
                HasAll(contract.outputs, ERenderOutput::Depth | ERenderOutput::Motion);
            const bool supportsRequestedPostProcess = HasAny(contract.post, EPostProcess::Upscale) &&
                (!typeInfo.requiresRayReconstruction ||
                 HasAny(contract.post, EPostProcess::RayReconstruction));
            const bool hasRequiredSwapchainUsage = !typeInfo.requiresStorageOutput ||
                frame_.swapChain->SupportsUsage(VK_IMAGE_USAGE_STORAGE_BIT);
            temporalSuperResolutionActive_ =
                resolvedMode.enabled && requestedUpscalerType != Rendering::Upscaler::EUpscalerType::None &&
                upscaler_ && SupportsUpscaler(requestedUpscalerType) && hasRequiredOutputs &&
                supportsRequestedPostProcess && hasRequiredSwapchainUsage;

            if (temporalSuperResolutionActive_)
            {
                activeUpscalerType_ = requestedUpscalerType;
                upscaler_->SetActiveType(activeUpscalerType_);
                const auto optimal = upscaler_->GetOptimalRenderSettings(
                    effectiveSuperResolutionMode_,
                    frame_.swapChain->Extent(),
                    true,
                    frame_.swapChain->IsHDR(),
                    activeUpscalerType_);
                renderExtent = optimal.renderExtent;
                SPDLOG_INFO("{} active for {}: {}x{} -> {}x{}",
                            typeInfo.name,
                            GetRendererName(logicRenderers_.current),
                            renderExtent.width, renderExtent.height,
                            frame_.swapChain->Extent().width,
                            frame_.swapChain->Extent().height);
            }
            else if (requestedUpscalerType != Rendering::Upscaler::EUpscalerType::None)
            {
                SPDLOG_WARN("{} is unavailable for {}; using native rendering",
                            typeInfo.name, GetRendererName(logicRenderers_.current));
            }
        }

        if (upscaler_ && !temporalSuperResolutionActive_)
        {
            upscaler_->SetActiveType(Rendering::Upscaler::EUpscalerType::None);
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

        // Deliberately serialized frame model. Per-frame acquire/fence/command resources use one
        // slot; present semaphores and UBOs remain indexed by acquired swapchain image.
        for (uint32_t frameSlot = 0; frameSlot < kFramesInFlight; ++frameSlot)
        {
            frame_.imageAvailableSemaphores.emplace_back(*ctx_.device);
            frame_.inFlightFences.emplace_back(*ctx_.device, true);
            frame_.inFlightFenceSubmitSerials.emplace_back(0);
        }
        for (size_t i = 0; i != frame_.swapChain->ImageViews().size(); ++i)
        {
            frame_.renderFinishedSemaphores.emplace_back(*ctx_.device);
            frame_.uniformBuffers.emplace_back(*ctx_.device);
        }

        // commandbuffer
        frame_.commandBuffers.reset(new CommandBuffers(*ctx_.commandPool, kFramesInFlight));

        frame_.recordingSubmitSerial = 0;
        frame_.queuedSignalSemaphores.clear();
        frame_.queuedSignalValues.clear();

        // Shared render images.
        CreateRenderImages();
        renderViews_->CreateSwapChain(SwapChain());
        CreateSceneSwapChainResources();

        // Delegate
        if (delegates_.createSwapChain)
        {
            delegates_.createSwapChain();
        }
    }

    void VulkanBaseRenderer::RefreshSceneSwapChainResources()
    {
        if (!frame_.swapChain)
        {
            return;
        }
        // The swapchain extent and upscaler selection remain valid across a scene-only
        // resource refresh. SetScene() already requests a temporal-history reset for
        // the next evaluation, so preserve the active upscaler mode here.
        renderViews_->ClearSchedule();

        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->DeleteSwapChain();
        }
        logicRenderers_.swapChainCreatedTypes.clear();

        overlay_.externalPasses.clear();
        overlay_.visibilityPipeline.reset();
        overlay_.visibilityFrameBuffer.reset();
        overlay_.sunShadowPass.reset();
        overlay_.wireframeFrameBuffers.clear();
        overlay_.wireframePipeline.reset();
        overlay_.bufferClearPipeline.reset();
        ambient_.softBake.reset();
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
        overlay_.temporalPostFilterPipeline.reset();
        overlay_.toneMappingPipeline.reset();
        overlay_.visualDebuggerPipeline.reset();

        CreateSceneSwapChainResources();
    }

    void VulkanBaseRenderer::DeleteSwapChain()
    {
        if (!frame_.swapChain)
        {
            return;
        }
        renderViews_->ClearSchedule();
        visibilityStateTracker_.Reset();
        swapchainStateTracker_.Reset();
        auxiliaryImageStateTracker_.Reset();
        shadowCameraFamilyCache_ = {};
        atmosphere_->DestroySwapChainPipelines();

        if (upscaler_)
        {
            upscaler_->OnSwapChainDestroyed();
        }

        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->DeleteSwapChain();
        }
        logicRenderers_.swapChainCreatedTypes.clear();
        renderViews_->DeleteSwapChain();

        if (delegates_.deleteSwapChain)
        {
            delegates_.deleteSwapChain();
        }

        overlay_.externalPasses.clear();

        for ( auto& storageImage : bindless_.images )
        {
            storageImage.reset();
        }
        overlay_.visibilityPipeline.reset();
        overlay_.visibilityFrameBuffer.reset();
        renderViews_->ResetSwapChainResources();
        // Auxiliary view resources reference view-bank images / the shared render pass; drop them
        // before the swapchain images go away.
        if (renderViewServices_)
        {
            renderViewServices_->OnSwapChainResourcesInvalidated(/*releaseOffscreenSampledOutputs*/ true);
        }
        overlay_.sunShadowPass.reset();
        
        screenshot_.image.reset();
        screenshot_.imageMemory.reset();
        screenshot_.captureRequested = false;
        screenshot_.captureReady = false;
        screenshot_.initialized = false;
        screenshot_.captureSubmitSerial = 0;
        frameGeneration_.hudlessImages.clear();
        temporalPostFilter_.pingImages.clear();
        temporalPostFilter_.pongImages.clear();
        temporalPostFilter_.pingInitialized.clear();
        temporalPostFilter_.pongInitialized.clear();
        lateToneMapping_.inputInitialized.clear();
        frame_.commandBuffers.reset();
        overlay_.wireframeFrameBuffers.clear();
        overlay_.wireframePipeline.reset();
        overlay_.bufferClearPipeline.reset();
        frame_.inFlightFenceSubmitSerials.clear();
        ambient_.softBake.reset();
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

        overlay_.temporalPostFilterPipeline.reset();
        overlay_.toneMappingPipeline.reset();
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
        ctx_.device->WaitIdle();
        DeleteSwapChain();
        CreateSwapChain();
        resetUpscalerHistory_ = true;
    }

    void VulkanBaseRenderer::ReloadShaders()
    {
        // Keep shader refresh on the same complete resource lifecycle used by resize/recovery.
        // A partial pipeline registry is faster only marginally and is easy to leave incomplete.
        RecreateSwapChain();
    }

    void VulkanBaseRenderer::CaptureScreenShot()
    {
        if (!screenshot_.captureReady)
        {
            Throw(std::runtime_error("screenshot capture was not recorded before present"));
        }
        screenshot_.captureReady = false;
    }

    // Camera-independent, runs once per scene per frame (shared across all views).
    void VulkanBaseRenderer::BeginSceneFrame(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        atmosphere_->BeginSceneFrame(commandBuffer, imageIndex);
        rayTracingSceneBackend_->PrepareSceneFrame(commandBuffer, imageIndex);
        ambientCubeBaker_->PrepareSceneFrame(commandBuffer, imageIndex);
    }

    void VulkanBaseRenderer::RenderViewToBank(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        RenderView& view,
        const bool clearSwapchain,
        LogicRendererBase& logicRenderer)
    {
        SCOPED_GPU_TIMER(view.DebugName());

        FActiveRenderViewScope activeViewScope(*this, view);
        const ERendererType rendererType = GetLogicRendererType(logicRenderer);
        const FRendererContract& contract = GetRendererContract(rendererType);
        if (view.SceneOverride() != nullptr && !contract.supportsSceneOverrideWithoutPrepare)
        {
            Throw(std::runtime_error(fmt::format(
                "renderer {} cannot render SceneOverride without prepared scene-local resources",
                GetRendererName(rendererType))));
        }
        const bool initializePrevDepthBeforeCull =
            HasAny(contract.prepasses, EViewPrepass::Cull) && !view.IsPrimary() && !view.PrevDepthValid();
        if (initializePrevDepthBeforeCull)
        {
            DispatchClearPass(commandBuffer, imageIndex, /*clearSwapchain*/ false);
        }

        PreRenderPerView(commandBuffer, imageIndex, clearSwapchain, contract);
        atmosphere_->PrepareView(commandBuffer, imageIndex, view.IsPrimary(), contract);
        logicRenderer.Render(commandBuffer, imageIndex);
        atmosphere_->ApplyToView(commandBuffer, imageIndex, view.IsPrimary(), contract);
        if (view.CopyObjectIdHistory() && HasAny(contract.history, EHistoryChannel::ObjectId))
        {
            CopyObjectIdHistory(commandBuffer);
        }
        if (initializePrevDepthBeforeCull)
        {
            view.SetPrevDepthValid(true);
        }
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
            RenderView* view = renderViews_->Resolve(item.view);
            if (view == nullptr || item.logicRenderer == nullptr)
            {
                if (view == nullptr)
                {
                    SPDLOG_WARN("Skipping stale RenderView schedule item (bank {}, generation {})",
                                item.view.bankBase, item.view.generation);
                }
                continue;
            }

            RenderViewToBank(commandBuffer, imageIndex, *view, item.clearSwapchain, *item.logicRenderer);
            renderedAny = true;
            if (item.postRender)
            {
                item.postRender(*view);
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
    // FViewRenderContext before calling). Only the primary view
    // owns the swapchain, so its clear pass also clears the swapchain image.
    void VulkanBaseRenderer::PreRenderPerView(VkCommandBuffer commandBuffer, const uint32_t imageIndex,
                                              const bool isPrimaryView, const FRendererContract& contract)
    {
        gpuDrivenPasses_->RenderViewPrepasses(commandBuffer, imageIndex, isPrimaryView, contract);
    }

    VkDeviceAddress VulkanBaseRenderer::AtmosphereParamsAddress() const
    {
        return atmosphere_ ? atmosphere_->ParamsAddress() : 0;
    }

    glm::vec3 VulkanBaseRenderer::AtmosphereTransmittanceToSun(
        float cameraAltitudeKm, float sunZenithCosine) const
    {
        return atmosphere_
            ? atmosphere_->TransmittanceToSun(cameraAltitudeKm, sunZenithCosine)
            : glm::vec3(1.0f);
    }

    void VulkanBaseRenderer::DrawFrame()
    {
        if (requestRecreateSwapChain_)
        {
            RecreateSwapChain();
            requestRecreateSwapChain_ = false;
            return;
        }
        
        const auto noTimeout = std::numeric_limits<uint64_t>::max();
        auto* engine = NextEngine::GetInstance();
        frameSettings_.userSettings = engine->GetUserSettings();
        frameSettings_.progressiveRendering = engine->IsProgressiveRendering();
        frameSettings_.offlineProgressivePathTracing = engine->IsOfflineProgressivePathTracing();
        frameSettings_.effectiveSharc = engine->IsEffectiveSharcEnabled();
        frameSettings_.progressiveAccumulatedFrames = engine->GetProgressiveRenderAccumulatedFrames();
        frameSettings_.progressiveTargetFrames = engine->GetProgressiveRenderTargetFrames();
        const auto& settings = frameSettings_.userSettings;
        const bool frameGenerationEnabled = !frameSettings_.progressiveRendering && settings.FrameGeneration &&
            SupportsFrameGeneration(activeUpscalerType_) && temporalSuperResolutionActive_ &&
            engine->GetEngineStatus() != NextRenderer::EApplicationStatus::Loading;

        frame_.streamlineFrameToken = upscaler_
            ? upscaler_->BeginFrame(static_cast<uint32_t>(frame_.frameCount),
                                    frameGenerationEnabled,
                                    frameGenerationEnabled ? settings.FrameGenerationFrameLimitFps : 0)
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
            ctx_.frameProfiler->EndGpuFrame((*frame_.commandBuffers)[frame_.currentFrame]);
        }

        VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
        if (!FrameSubmission::WaitAndAcquire(
                *this, noTimeout, imageAvailableSemaphore, renderFinishedSemaphore))
        {
            return;
        }

        frame_.recordingSubmitSerial = frame_.nextSubmitSerial;
        frame_.queuedSignalSemaphores.clear();
        frame_.queuedSignalValues.clear();

        // CPU-side node sync must run after the previous frame's GPU work has
        // completed so the stats readback reflects finished primitives. Runtime
        // node creation can grow the expanded primitive count, so capacity is
        // re-checked against the freshly computed requirement right after.
        bool needAfterUpdateScene = false;
        {
            SCOPED_CPU_TIMER("update nodes");
            needAfterUpdateScene = GetScene().UpdateNodes();
        }
        {
            SCOPED_CPU_TIMER("prepare gpudriven");
            if (GetScene().EnsureGpuDrivenBufferCapacity(*ctx_.commandPool))
            {
                needAfterUpdateScene = true;
            }
        }
        if (needAfterUpdateScene)
        {
            AfterUpdateScene();
        }

        {
            SCOPED_CPU_TIMER("update uniform");
            UpdateUniformBuffer(frame_.currentImageIndex);
        }

        const auto commandBuffer = frame_.commandBuffers->Begin(frame_.currentFrame);
        ctx_.frameProfiler->BeginGpuFrame(commandBuffer);

        {
            SCOPED_GPU_TIMER("[gpu]");

            {
                SCOPED_CPU_TIMER("begin scene");
                SCOPED_GPU_TIMER("[pre-render]");
                BeginSceneFrame(commandBuffer, frame_.currentImageIndex);
                skin_.updateRequests.clear();
            }

            {
                SCOPED_CPU_TIMER("render");
                SCOPED_GPU_TIMER("[render]");
                Render(commandBuffer, frame_.currentImageIndex);
            }

            {
                SCOPED_GPU_TIMER("[post-render]");
                PostRender(commandBuffer, frame_.currentImageIndex);
            }

            if (upscaler_ && frameGenerationEnabled)
            {
                SCOPED_GPU_TIMER("frame-generation-prepare");
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
                inputs.enableFrameGeneration = true;
                const bool transitionDepth = Rendering::Upscaler::GetUpscalerTypeInfo(
                    static_cast<uint32_t>(activeUpscalerType_)).frameGenerationRequiresReadableDepth;
                if (transitionDepth)
                {
                    VkImageSubresourceRange depthRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                    ImageMemoryBarrier::Insert(
                        commandBuffer,
                        DepthBuffer().GetImage().Handle(),
                        depthRange,
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
                    inputs.depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                }
                upscaler_->TagFrameGeneration(inputs);
                if (transitionDepth)
                {
                    VkImageSubresourceRange depthRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                    ImageMemoryBarrier::Insert(
                        commandBuffer,
                        DepthBuffer().GetImage().Handle(),
                        depthRange,
                        VK_ACCESS_SHADER_READ_BIT,
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
                }
            }

            if (delegates_.postRender)
            {
                SCOPED_GPU_TIMER("ui");
                delegates_.postRender(commandBuffer, frame_.currentImageIndex);
            }
            // The main UI render pass loads from PRESENT_SRC and finishes in PRESENT_SRC.
            // Frame consumers invoked by the delegate follow the same external contract.
            ImportSwapchainImageState(frame_.currentImageIndex, {
                .initialized = true,
                .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .stages = PipelineCommon::ERenderStage::Present,
                .access = PipelineCommon::EResourceAccess::None,
                .lastPass = "UI and frame consumers",
            });

            if (screenshot_.captureRequested)
            {
                SCOPED_GPU_TIMER("screenshot");
                VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                TransitionSwapchainImage(
                    commandBuffer, frame_.currentImageIndex,
                    {.stages = PipelineCommon::ERenderStage::Transfer,
                     .access = PipelineCommon::EResourceAccess::TransferRead,
                     .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                    "screenshot source");
                ImageMemoryBarrier::Insert(
                    commandBuffer,
                    screenshot_.initialized ? VK_PIPELINE_STAGE_TRANSFER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT, screenshot_.image->Handle(), range,
                    screenshot_.initialized ? VK_ACCESS_TRANSFER_WRITE_BIT : 0,
                    VK_ACCESS_TRANSFER_WRITE_BIT,
                    screenshot_.initialized ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

                VkImageCopy copyRegion{};
                copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copyRegion.extent = {SwapChain().Extent().width, SwapChain().Extent().height, 1};
                vkCmdCopyImage(commandBuffer, frame_.swapChain->Images()[frame_.currentImageIndex],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               screenshot_.image->Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               1, &copyRegion);
                TransitionSwapchainImage(
                    commandBuffer, frame_.currentImageIndex,
                    {.stages = PipelineCommon::ERenderStage::Present,
                     .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                    "screenshot present");
                screenshot_.captureRequested = false;
                screenshot_.captureReady = true;
                screenshot_.initialized = true;
                screenshot_.captureSubmitSerial = frame_.recordingSubmitSerial;
            }
        }
        frame_.commandBuffers->End(frame_.currentFrame);

        if (upscaler_ && frame_.streamlineFrameToken)
        {
            upscaler_->MarkFrame(Rendering::Upscaler::EFrameMarker::RenderSubmitStart,
                                 frame_.streamlineFrameToken);
        }
        {
            SCOPED_CPU_TIMER("submit");
            FrameSubmission::Submit(*this, commandBuffer, imageAvailableSemaphore, renderFinishedSemaphore);
        }
        if (upscaler_ && frame_.streamlineFrameToken)
        {
            upscaler_->MarkFrame(Rendering::Upscaler::EFrameMarker::RenderSubmitEnd,
                                 frame_.streamlineFrameToken);
        }

        if (upscaler_ && frame_.streamlineFrameToken)
        {
            upscaler_->MarkFrame(Rendering::Upscaler::EFrameMarker::PresentStart,
                                 frame_.streamlineFrameToken);
        }
        {
            SCOPED_CPU_TIMER("present");
            if (!FrameSubmission::Present(*this, renderFinishedSemaphore))
            {
                return;
            }
        }
        if (upscaler_ && frame_.streamlineFrameToken)
        {
            upscaler_->MarkFrame(Rendering::Upscaler::EFrameMarker::PresentEnd,
                                 frame_.streamlineFrameToken);
        }
        
        if (delegates_.afterSubmit)
        {
            SCOPED_CPU_TIMER("after submit");
            delegates_.afterSubmit();
        }

        frame_.currentFrame = (frame_.currentFrame + 1) % frame_.inFlightFences.size();
        resetUpscalerHistory_ = false;
        frame_.frameCount++;
    }

    void VulkanBaseRenderer::BeforeNextFrame()
    {
        for (auto& logicRenderer : logicRenderers_.renderers)
        {
            logicRenderer.second->BeforeNextFrame();
        }

        if (renderViewServices_)
        {
            renderViewServices_->BeforeNextFrame();
        }

        if (delegates_.beforeNextTick)
        {
            delegates_.beforeNextTick();
        }
    }

    void VulkanBaseRenderer::RegisterLogicRenderer(ERendererType type)
    {
        if (!IsLogicRendererRegistered(type))
        {
            logicRenderers_.registeredTypes.push_back(type);
        }
        logicRenderers_.current = type;
    }

    bool VulkanBaseRenderer::IsLogicRendererRegistered(ERendererType type) const
    {
        return std::find(logicRenderers_.registeredTypes.begin(), logicRenderers_.registeredTypes.end(), type) !=
               logicRenderers_.registeredTypes.end();
    }

    ERendererType VulkanBaseRenderer::GetLogicRendererType(const LogicRendererBase& renderer) const
    {
        const auto found = std::find_if(logicRenderers_.renderers.begin(), logicRenderers_.renderers.end(),
            [&renderer](const auto& entry) { return entry.second.get() == &renderer; });
        if (found == logicRenderers_.renderers.end())
        {
            Throw(std::runtime_error("logic renderer is not registered"));
        }
        return found->first;
    }

    LogicRendererBase* VulkanBaseRenderer::EnsureLogicRenderer(ERendererType type)
    {
        if (!IsLogicRendererRegistered(type))
        {
            return nullptr;
        }

        auto renderer = logicRenderers_.renderers.find(type);
        if (renderer != logicRenderers_.renderers.end())
        {
            if (ctx_.device && frame_.swapChain)
            {
                EnsureLogicRendererSwapChain(type, *renderer->second);
            }
            return renderer->second.get();
        }

        auto logicRenderer = GetRendererDescriptor(type).factory(*this);
        LogicRendererBase* result = logicRenderer.get();
        logicRenderers_.renderers[type] = std::move(logicRenderer);
        if (ctx_.device)
        {
            result->OnDeviceSet();
        }
        if (frame_.swapChain)
        {
            EnsureLogicRendererSwapChain(type, *result);
        }
        return result;
    }

    void VulkanBaseRenderer::EnsureLogicRendererSwapChain(ERendererType type, LogicRendererBase& logicRenderer)
    {
        if (!frame_.swapChain || logicRenderers_.swapChainCreatedTypes.contains(type))
        {
            return;
        }

        logicRenderer.CreateSwapChain(frame_.swapChain->RenderExtent());
        logicRenderers_.swapChainCreatedTypes.insert(type);
    }

    FRendererRequirements VulkanBaseRenderer::CurrentRendererRequirements() const
    {
        return GetRendererRequirements(logicRenderers_.current);
    }

    FRendererRequirements VulkanBaseRenderer::ActiveRendererRequirements() const
    {
        FRendererRequirements requirements = CurrentRendererRequirements();
        if (GOption->ReferenceMode)
        {
            for (const ERendererType type : GetReferenceRendererTypes())
            {
                if (IsLogicRendererRegistered(type))
                {
                    requirements.Merge(GetRendererRequirements(type));
                }
            }
        }
        return requirements;
    }

    FRendererRequirements VulkanBaseRenderer::RegisteredRendererRequirements() const
    {
        FRendererRequirements requirements;
        for (const ERendererType type : logicRenderers_.registeredTypes)
        {
            requirements.Merge(GetRendererRequirements(type));
        }
        return requirements;
    }

    void VulkanBaseRenderer::SwitchLogicRenderer(ERendererType type)
    {
        const ERendererType previousType = logicRenderers_.current;
        LogicRendererBase* logicRenderer = EnsureLogicRenderer(type);
        if (logicRenderer == nullptr)
        {
            SPDLOG_WARN("Skipping switch to unregistered logic renderer {}", GetRendererName(type));
            return;
        }

        logicRenderers_.current = type;
        EnsureLogicRendererSwapChain(type, *logicRenderer);
        if (type != previousType)
        {
            renderViews_->InvalidateAllTemporalHistory(EHistoryInvalidationReason::RendererChanged);
            resetUpscalerHistory_ = true;
            SPDLOG_INFO("Renderer history invalidated: {} -> {}, generation {}",
                        GetRendererName(previousType), GetRendererName(type),
                        PrimaryView().State().historyGeneration);
        }
    }

    void VulkanBaseRenderer::ComposeViewToSwapchainSubrect(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        RenderView& view)
    {
        const RenderImage* sceneColorImage = GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_SCENE_COLOR);
        if (sceneColorImage == nullptr)
        {
            return;
        }

        TransitionViewImages(commandBuffer, view, {
            {Assets::Bindless::RT_SCENE_COLOR, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead},
        }, "compose view subrect source");

        const VkRect2D subrect = view.Desc().subrect;
        ApplyToneMappingAfterUpscaler(
            commandBuffer,
            imageIndex,
            false,
            view.RtBankBase(),
            view.RenderExtent(),
            subrect.extent,
            subrect.offset,
            view.State().previousUniformBuffer);
    }

    bool VulkanBaseRenderer::PrepareTemporalPostFilterOutput(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        Rendering::Upscaler::FFrameInputs& inputs)
    {
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();
        if (!settings.TemporalUpscalerPostFilter || overlay_.temporalPostFilterPipeline == nullptr ||
            imageIndex >= temporalPostFilter_.pingImages.size() ||
            imageIndex >= temporalPostFilter_.pingInitialized.size())
        {
            return false;
        }

        RenderImage& output = *temporalPostFilter_.pingImages[imageIndex];
        const bool initialized = temporalPostFilter_.pingInitialized[imageIndex];
        constexpr VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        ImageMemoryBarrier::Insert(
            commandBuffer,
            initialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            output.GetImage().Handle(),
            range,
            initialized ? VK_ACCESS_SHADER_READ_BIT : 0u,
            VK_ACCESS_SHADER_WRITE_BIT,
            initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);
        temporalPostFilter_.pingInitialized[imageIndex] = true;
        inputs.scalingOutputColor = MakeRenderImageResource(
            &output, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_USAGE_STORAGE_BIT);
        return true;
    }

    void VulkanBaseRenderer::ApplyTemporalPostFilter(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        const Rendering::Upscaler::FFrameInputs& inputs)
    {
        SCOPED_GPU_TIMER("Temporal a-trous filter");
        if (imageIndex >= temporalPostFilter_.pingImages.size() ||
            imageIndex >= temporalPostFilter_.pongImages.size() ||
            imageIndex >= temporalPostFilter_.pingInitialized.size() ||
            imageIndex >= temporalPostFilter_.pongInitialized.size() ||
            overlay_.temporalPostFilterPipeline == nullptr)
        {
            return;
        }

        constexpr VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();
        struct FPushConstants
        {
            uint32_t inputIndex;
            uint32_t outputIndex;
            int32_t outputOffsetX;
            int32_t outputOffsetY;
            uint32_t outputWidth;
            uint32_t outputHeight;
            float strength;
            float lumaSigma;
            float fireflySigma;
            uint32_t stepWidth;
            uint32_t applyFireflyClamp;
            uint32_t finalPass;
        };
        static_assert(sizeof(FPushConstants) == 48);

        RenderImage* sourceImage = temporalPostFilter_.pingImages[imageIndex].get();
        uint32_t sourceIndex = Assets::Bindless::RT_TEMPORAL_POST_PING0 + imageIndex;
        const uint32_t passCount = std::clamp(settings.TemporalUpscalerPostFilterPasses, 1u, 4u);
        const float totalStrength = std::clamp(settings.TemporalUpscalerPostFilterStrength, 0.0f, 1.0f);
        const float passStrength =
            1.0f - std::pow(1.0f - totalStrength, 1.0f / static_cast<float>(passCount));
        for (uint32_t pass = 0; pass < passCount; ++pass)
        {
            ImageMemoryBarrier::Insert(
                commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                sourceImage->GetImage().Handle(),
                range,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_GENERAL);

            const bool finalPass = pass + 1u == passCount;
            RenderImage* destinationImage = nullptr;
            uint32_t destinationIndex = Assets::Bindless::RT_SWAPCHAIN0 + imageIndex;
            int32_t destinationOffsetX = inputs.outputOffset.x;
            int32_t destinationOffsetY = inputs.outputOffset.y;
            if (finalPass)
            {
                destinationImage = bindless_.images[Assets::Bindless::RT_TONEMAP_INPUT0 + imageIndex].get();
                destinationIndex = Assets::Bindless::RT_TONEMAP_INPUT0 + imageIndex;
                destinationOffsetX = 0;
                destinationOffsetY = 0;
                const bool initialized = lateToneMapping_.inputInitialized[imageIndex];
                ImageMemoryBarrier::Insert(
                    commandBuffer,
                    initialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    destinationImage->GetImage().Handle(),
                    range,
                    initialized ? VK_ACCESS_SHADER_READ_BIT : 0u,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL);
                lateToneMapping_.inputInitialized[imageIndex] = true;
            }
            else
            {
                const bool sourceIsPing = sourceIndex == Assets::Bindless::RT_TEMPORAL_POST_PING0 + imageIndex;
                destinationImage = sourceIsPing
                    ? temporalPostFilter_.pongImages[imageIndex].get()
                    : temporalPostFilter_.pingImages[imageIndex].get();
                destinationIndex = (sourceIsPing
                    ? Assets::Bindless::RT_TEMPORAL_POST_PONG0
                    : Assets::Bindless::RT_TEMPORAL_POST_PING0) + imageIndex;
                const bool destinationInitialized = sourceIsPing
                    ? temporalPostFilter_.pongInitialized[imageIndex]
                    : temporalPostFilter_.pingInitialized[imageIndex];
                ImageMemoryBarrier::Insert(
                    commandBuffer,
                    destinationInitialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    destinationImage->GetImage().Handle(),
                    range,
                    destinationInitialized ? VK_ACCESS_SHADER_READ_BIT : 0u,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    destinationInitialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL);
                if (sourceIsPing)
                {
                    temporalPostFilter_.pongInitialized[imageIndex] = true;
                }
                else
                {
                    temporalPostFilter_.pingInitialized[imageIndex] = true;
                }
                destinationOffsetX = 0;
                destinationOffsetY = 0;
            }

            const FPushConstants pushConstants{
                sourceIndex,
                destinationIndex,
                destinationOffsetX,
                destinationOffsetY,
                inputs.outputExtent.width,
                inputs.outputExtent.height,
                passStrength,
                settings.TemporalUpscalerPostFilterLumaSigma,
                settings.TemporalUpscalerFireflySigma,
                1u << pass,
                pass == 0u ? 1u : 0u,
                finalPass ? 1u : 0u,
            };
            overlay_.temporalPostFilterPipeline->BindPipeline(commandBuffer, &pushConstants);
            vkCmdDispatch(commandBuffer,
                          (inputs.outputExtent.width + 7u) / 8u,
                          (inputs.outputExtent.height + 7u) / 8u,
                          1);

            if (!finalPass)
            {
                sourceImage = destinationImage;
                sourceIndex = destinationIndex;
            }
        }
    }

    void VulkanBaseRenderer::ApplyToneMappingAfterUpscaler(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        const bool sourceIsUpscaled,
        const uint32_t sourceViewBankBase,
        const VkExtent2D sourceExtent,
        const VkExtent2D outputExtent,
        const VkOffset2D outputOffset,
        const Assets::UniformBufferObject& outputUbo)
    {
        if (overlay_.toneMappingPipeline == nullptr ||
            imageIndex >= lateToneMapping_.inputInitialized.size())
        {
            return;
        }

        SCOPED_GPU_TIMER("tone mapping after upscaler");
        constexpr VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        const bool writesSwapchain = SwapChain().SupportsUsage(VK_IMAGE_USAGE_STORAGE_BIT);
        uint32_t inputIndex = Assets::Bindless::RT_SCENE_COLOR;
        VkExtent2D inputExtent = sourceExtent;
        if (sourceIsUpscaled)
        {
            inputIndex = Assets::Bindless::RT_TONEMAP_INPUT0 + imageIndex;
            inputExtent = outputExtent;
            RenderImage* inputImage = bindless_.images[inputIndex].get();
            if (inputImage == nullptr)
            {
                return;
            }
            ImageMemoryBarrier::Insert(
                commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                inputImage->GetImage().Handle(),
                range,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_GENERAL);
        }

        const uint32_t outputIndex = writesSwapchain
            ? Assets::Bindless::RT_SWAPCHAIN0 + imageIndex
            : Assets::Bindless::RT_TONEMAP_INPUT0 + imageIndex;
        RenderImage* offscreenOutput = writesSwapchain
            ? nullptr
            : bindless_.images[Assets::Bindless::RT_TONEMAP_INPUT0 + imageIndex].get();
        if (!writesSwapchain && offscreenOutput == nullptr)
        {
            return;
        }
        if (offscreenOutput != nullptr)
        {
            const bool initialized = lateToneMapping_.inputInitialized[imageIndex];
            ImageMemoryBarrier::Insert(
                commandBuffer,
                initialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT
                             : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                offscreenOutput->GetImage().Handle(),
                range,
                initialized ? VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT : 0u,
                VK_ACCESS_SHADER_WRITE_BIT,
                initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL);
            lateToneMapping_.inputInitialized[imageIndex] = true;
        }

        TransitionSwapchainImage(
            commandBuffer, imageIndex,
            {.stages = writesSwapchain ? PipelineCommon::ERenderStage::Compute
                                       : PipelineCommon::ERenderStage::Transfer,
             .access = writesSwapchain ? PipelineCommon::EResourceAccess::ShaderWrite
                                       : PipelineCommon::EResourceAccess::TransferWrite,
             .layout = VK_IMAGE_LAYOUT_GENERAL},
            "tone mapping after upscaler");

        struct FPushConstants
        {
            uint32_t inputIndex;
            uint32_t inputWidth;
            uint32_t inputHeight;
            uint32_t outputWidth;
            uint32_t outputHeight;
            int32_t outputOffsetX;
            int32_t outputOffsetY;
            uint32_t outputIndex;
            uint32_t sourceIsUpscaled;
            float paperWhiteNit;
            uint32_t hdrOutputMode;
            uint32_t hdr;
            uint32_t hasSky;
        };
        static_assert(sizeof(FPushConstants) == 52);

        const FPushConstants pushConstants{
            inputIndex,
            inputExtent.width,
            inputExtent.height,
            outputExtent.width,
            outputExtent.height,
            outputOffset.x,
            outputOffset.y,
            outputIndex,
            sourceIsUpscaled ? 1u : 0u,
            outputUbo.PaperWhiteNit,
            outputUbo.HDROutputMode,
            outputUbo.HDR,
            outputUbo.HasSky,
        };
        overlay_.toneMappingPipeline->BindPipeline(commandBuffer, &pushConstants, sourceViewBankBase);
        vkCmdDispatch(
            commandBuffer,
            (outputExtent.width + 7u) / 8u,
            (outputExtent.height + 7u) / 8u,
            1);

        if (offscreenOutput != nullptr)
        {
            ImageMemoryBarrier::Insert(
                commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                offscreenOutput->GetImage().Handle(),
                range,
                VK_ACCESS_SHADER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_GENERAL);
            VkImageBlit blitRegion{};
            blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blitRegion.srcOffsets[0] = {
                outputOffset.x,
                outputOffset.y,
                0};
            blitRegion.srcOffsets[1] = {
                outputOffset.x + static_cast<int32_t>(outputExtent.width),
                outputOffset.y + static_cast<int32_t>(outputExtent.height),
                1};
            blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            blitRegion.dstOffsets[0] = {
                outputOffset.x,
                outputOffset.y,
                0};
            blitRegion.dstOffsets[1] = {
                outputOffset.x + static_cast<int32_t>(outputExtent.width),
                outputOffset.y + static_cast<int32_t>(outputExtent.height),
                1};
            vkCmdBlitImage(
                commandBuffer,
                offscreenOutput->GetImage().Handle(),
                VK_IMAGE_LAYOUT_GENERAL,
                SwapChain().Images()[imageIndex],
                VK_IMAGE_LAYOUT_GENERAL,
                1,
                &blitRegion,
                VK_FILTER_LINEAR);
        }
    }

    void VulkanBaseRenderer::ResolvePrimaryViewToSwapchain(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        const FRendererContract& contract = GetRendererContract(logicRenderers_.current);
        TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_SCENE_COLOR,
             PipelineCommon::ERenderStage::Compute | PipelineCommon::ERenderStage::Transfer,
             PipelineCommon::EResourceAccess::ShaderRead | PipelineCommon::EResourceAccess::TransferRead},
            {Assets::Bindless::RT_MOTIONVECTOR,
             PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_ALBEDO,
             PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead},
            {Assets::Bindless::RT_NORMAL,
             PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead},
        }, "resolve primary view source");

        bool resolvedByUpscaler = false;
        const bool temporalUpscalerActive =
            !frameSettings_.progressiveRendering &&
            HasAny(contract.post, EPostProcess::Upscale) && temporalSuperResolutionActive_;
        if (upscaler_ && temporalUpscalerActive)
        {
            SCOPED_GPU_TIMER("temporal upscaler resolve");
            TransitionSwapchainImage(
                commandBuffer, imageIndex,
                {.stages = PipelineCommon::ERenderStage::Compute,
                 .access = PipelineCommon::EResourceAccess::ShaderWrite,
                 .layout = VK_IMAGE_LAYOUT_GENERAL},
                "temporal upscaler resolve");

            auto inputs = BuildUpscalerFrameInputs(commandBuffer, imageIndex, VK_IMAGE_LAYOUT_GENERAL);
            const auto& typeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
                static_cast<uint32_t>(activeUpscalerType_));
            inputs.inputColorIsLinear = true;
            RenderImage* toneMappingInput =
                bindless_.images[Assets::Bindless::RT_TONEMAP_INPUT0 + imageIndex].get();
            if (toneMappingInput == nullptr)
            {
                return;
            }
            inputs.scalingOutputColor = MakeRenderImageResource(
                toneMappingInput,
                VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
            const bool temporalPostFilterActive = typeInfo.supportsTemporalPostFilter &&
                PrepareTemporalPostFilterOutput(commandBuffer, imageIndex, inputs);
            if (!temporalPostFilterActive)
            {
                const bool initialized = lateToneMapping_.inputInitialized[imageIndex];
                constexpr VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                ImageMemoryBarrier::Insert(
                    commandBuffer,
                    initialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    toneMappingInput->GetImage().Handle(),
                    range,
                    initialized ? VK_ACCESS_SHADER_READ_BIT : 0u,
                    VK_ACCESS_SHADER_WRITE_BIT,
                    initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_GENERAL);
                lateToneMapping_.inputInitialized[imageIndex] = true;
            }
            if (typeInfo.requiresStorageOutput)
            {
                VkImageSubresourceRange depthRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                ImageMemoryBarrier::Insert(
                    commandBuffer,
                    DepthBuffer().GetImage().Handle(),
                    depthRange,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
                inputs.depth.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            }
            resolvedByUpscaler = upscaler_->Evaluate(inputs);
            if (typeInfo.leavesInputsShaderRead && resolvedByUpscaler)
            {
                // FidelityFX registers external inputs only for this dispatch (the same
                // lifetime semantics as Streamline's eOnlyValidNow tags), but its Vulkan
                // backend leaves sampled inputs in SHADER_READ_ONLY_OPTIMAL. Restore the
                // engine-owned layouts before the resources are reused on the next frame.
                constexpr VkImageSubresourceRange colorRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                ImageMemoryBarrier::Insert(
                    commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    inputs.scalingInputColor.image,
                    colorRange,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL);
                ImageMemoryBarrier::Insert(
                    commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    inputs.motionVectors.image,
                    colorRange,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL);

                VkImageSubresourceRange depthRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                ImageMemoryBarrier::Insert(
                    commandBuffer,
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    DepthBuffer().GetImage().Handle(),
                    depthRange,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            }
            else if (typeInfo.requiresStorageOutput)
            {
                VkImageSubresourceRange depthRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
                ImageMemoryBarrier::Insert(
                    commandBuffer,
                    DepthBuffer().GetImage().Handle(),
                    depthRange,
                    VK_ACCESS_SHADER_READ_BIT,
                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            }
            if (temporalPostFilterActive && resolvedByUpscaler)
            {
                ApplyTemporalPostFilter(commandBuffer, imageIndex, inputs);
            }
            else if (temporalPostFilterActive)
            {
                // The intermediate may contain a partially recorded failed dispatch. Discard it
                // before the next attempt instead of treating it as a readable previous result.
                temporalPostFilter_.pingInitialized[imageIndex] = false;
            }
        }
        ApplyToneMappingAfterUpscaler(
            commandBuffer,
            imageIndex,
            resolvedByUpscaler,
            0,
            SwapChain().RenderExtent(),
            SwapChain().OutputExtent(),
            SwapChain().OutputOffset(),
            frame_.lastUBO);
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
            const bool renderedAnyReferenceView = renderViewServices_ &&
                renderViewServices_->ScheduleReferenceViews(commandBuffer, imageIndex);
            DispatchScheduledRenderViews(commandBuffer, imageIndex);
            if (renderedAnyReferenceView)
            {
                TransitionSwapchainImage(
                    commandBuffer, imageIndex,
                    {.stages = PipelineCommon::ERenderStage::Present,
                     .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                    "reference present");
            }
            else
            {
                static bool warnedMissingReferenceProvider = false;
                if (!warnedMissingReferenceProvider)
                {
                    SPDLOG_WARN("Reference mode has no available view provider; presenting a cleared frame");
                    warnedMissingReferenceProvider = true;
                }
                DispatchClearPass(commandBuffer, imageIndex, /*clearSwapchain*/ true);
                TransitionSwapchainImage(
                    commandBuffer, imageIndex,
                    {.stages = PipelineCommon::ERenderStage::Present,
                     .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                    "empty reference present");
            }
        }
        else
        {
            if (LogicRendererBase* renderer = EnsureLogicRenderer(logicRenderers_.current))
            {
                RenderView& primary = preparePrimaryView("primary view");
                EnsureLogicRendererSwapChain(logicRenderers_.current, *renderer);
                ScheduleRenderView(primary, *renderer, /*clearSwapchain*/ true);
                DispatchScheduledRenderViews(commandBuffer, imageIndex);
            }

            // Module content passes run before debug overlay passes.
            for (const auto& externalPass : overlay_.externalPasses)
            {
                const FExternalPassContract passContract = externalPass->Contract();
                const uint32_t availableOutputs = static_cast<uint32_t>(
                    GetRendererContract(logicRenderers_.current).outputs);
                if (passContract.insertionPoint != EExternalPassInsertionPoint::AfterPrimaryView ||
                    passContract.scope != EExternalPassScope::PrimaryView ||
                    !AreExternalPassInputsAvailable(passContract, availableOutputs))
                {
                    SPDLOG_WARN("Skipping external pass {}: incompatible insertion/scope or missing outputs "
                                "(required=0x{:x}, available=0x{:x})",
                                passContract.name, passContract.requiredOutputs, availableOutputs);
                    continue;
                }
                SCOPED_GPU_TIMER("external pass");
                externalPass->Execute(commandBuffer, imageIndex);
            }

            if (NextEngine::GetInstance()->GetShowFlags().ShowWireframe)
            {
                DrawWireframeOverlay(commandBuffer, imageIndex);
            }

            ResolvePrimaryViewToSwapchain(commandBuffer, imageIndex);

            // Swapchain is in GENERAL here (before the present barrier). Auxiliary views render
            // through the same RenderViewManager schedule, then copy/compose their outputs.
            if (renderViewServices_ && renderViewServices_->HasWork())
            {
                renderViewServices_->ScheduleViews(commandBuffer, imageIndex);
                DispatchScheduledRenderViews(commandBuffer, imageIndex);
            }
            if (renderViewServices_)
            {
                renderViewServices_->ClearOffscreenFrameRequests();
            }

            TransitionSwapchainImage(
                commandBuffer, imageIndex,
                {.stages = PipelineCommon::ERenderStage::Present,
                 .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                "render present");
        }
    }

    void VulkanBaseRenderer::TransitionSwapchainImage(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        const PipelineCommon::FImageUse& requestedUse,
        const std::string_view passName)
    {
        if (imageIndex >= SwapChain().Images().size())
        {
            Throw(std::out_of_range("swapchain state transition image index"));
        }
        PipelineCommon::FImageUse use = requestedUse;
        use.image = {static_cast<uint64_t>(imageIndex) + 1u};
        if (const auto barrier = swapchainStateTracker_.Use(use, passName))
        {
            ImageMemoryBarrier::Insert(commandBuffer, barrier->srcStages, barrier->dstStages,
                                       SwapChain().Images()[imageIndex], barrier->range,
                                       barrier->srcAccess, barrier->dstAccess,
                                       barrier->oldLayout, barrier->newLayout);
        }
    }

    void VulkanBaseRenderer::ImportSwapchainImageState(
        const uint32_t imageIndex,
        const PipelineCommon::FImageState& state)
    {
        if (imageIndex >= SwapChain().Images().size())
        {
            Throw(std::out_of_range("swapchain state import image index"));
        }
        swapchainStateTracker_.Import({static_cast<uint64_t>(imageIndex) + 1u}, state);
    }

    void VulkanBaseRenderer::TransitionActiveViewImages(
        VkCommandBuffer commandBuffer,
        const std::initializer_list<FViewImageUse> uses,
        const std::string_view passName)
    {
        TransitionViewImages(commandBuffer, ActiveRenderView(), uses, passName);
    }

    void VulkanBaseRenderer::TransitionViewImages(
        VkCommandBuffer commandBuffer,
        RenderView& view,
        const std::initializer_list<FViewImageUse> uses,
        const std::string_view passName)
    {
        auto& tracker = view.ResourceStates();
        const uint32_t viewBase = view.RtBankBase();
        std::vector<VkImageMemoryBarrier> barriers;
        barriers.reserve(uses.size());
        VkPipelineStageFlags srcStages = 0;
        VkPipelineStageFlags dstStages = 0;

        for (const FViewImageUse& requested : uses)
        {
            const uint32_t absoluteId = viewBase + requested.bindlessId;
            const RenderImage* image = GetStorageImage(absoluteId);
            if (image == nullptr)
            {
                Throw(std::runtime_error(fmt::format("missing view image {} for {}", absoluteId, passName)));
            }
            const auto barrier = tracker.Use({
                .image = {static_cast<uint64_t>(absoluteId) + 1u},
                .stages = requested.stages,
                .access = requested.access,
                .layout = requested.layout,
                .discardPreviousContents = requested.discardPreviousContents,
            }, passName);
            if (!barrier)
            {
                continue;
            }

            VkImageMemoryBarrier vkBarrier{};
            vkBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            vkBarrier.srcAccessMask = barrier->srcAccess;
            vkBarrier.dstAccessMask = barrier->dstAccess;
            vkBarrier.oldLayout = barrier->oldLayout;
            vkBarrier.newLayout = barrier->newLayout;
            vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkBarrier.image = image->GetImage().Handle();
            vkBarrier.subresourceRange = barrier->range;
            barriers.push_back(vkBarrier);
            srcStages |= barrier->srcStages;
            dstStages |= barrier->dstStages;
        }

        if (!barriers.empty())
        {
            vkCmdPipelineBarrier(commandBuffer, srcStages, dstStages, 0,
                                 0, nullptr, 0, nullptr,
                                 static_cast<uint32_t>(barriers.size()), barriers.data());
        }
    }

    VkDeviceAddress VulkanBaseRenderer::ActiveViewCameraAddress(const uint32_t imageIndex) const
    {
        if (activeViewContext_.cameraAddress != 0)
        {
            return activeViewContext_.cameraAddress;
        }
        return frame_.uniformBuffers[imageIndex].Buffer().GetDeviceAddress();
    }

    void VulkanBaseRenderer::PostRender(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        const auto& settings = NextEngine::GetInstance()->GetUserSettings();

        if (ActiveRendererRequirements().requestAmbientCube && !ShouldSkipAmbientCubeUpdates())
        {
            if (settings.BakeSpeedLevel != 2)
            {
                const bool useHardware = caps_.supportRayTracing && !GOption->ForceSoftGen;
                BakeAmbientCubeCascade(commandBuffer, imageIndex, useHardware);
            }
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
        inputs.upscalerType = temporalSuperResolutionActive_
            ? activeUpscalerType_
            : Rendering::Upscaler::EUpscalerType::None;
        inputs.enableFrameGeneration = settings.FrameGeneration &&
            SupportsFrameGeneration(inputs.upscalerType);
        inputs.superResolutionMode = effectiveSuperResolutionMode_;
        inputs.frameGenerationMultiplier = std::clamp(settings.FrameGenerationMultiplier, 2u, 4u);
        inputs.hdrOutput = swapChain.IsHDR();
        inputs.frameTimeDeltaMilliseconds = static_cast<float>(
            std::max(NextEngine::GetInstance()->GetDeltaSeconds() * 1000.0, 0.001));
        inputs.nativeTemporalHistoryWeight = settings.NativeTAAUHistoryWeight;
        inputs.nativeTemporalSharpness = settings.NativeTAAUSharpness;
        inputs.renderExtent = swapChain.RenderExtent();
        inputs.outputExtent = swapChain.OutputExtent();
        inputs.outputOffset = swapChain.OutputOffset();
        inputs.swapchainFormat = swapChain.Format();
        inputs.backBufferCount = static_cast<uint32_t>(swapChain.Images().size());
        inputs.swapchain = swapChain.Handle();
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
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
        inputs.scalingInputColor = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_SCENE_COLOR),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
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
            GetViewStorageImage(Assets::Bindless::RT_SINGLE_DIFFUSE),
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        inputs.specularNoisy = MakeRenderImageResource(
            GetViewStorageImage(Assets::Bindless::RT_SINGLE_SPECULAR),
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
        if (renderViews_)
        {
            renderViews_->ClearSchedule();
        }
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
        auto& nodeTrans = scene.GetNodeProxies();
        if (nodeTrans.empty() || rt_->blas.empty())
        {
            rt_->tlasUpdateRequest = 0;
            return;
        }

        instances.reserve(nodeTrans.size());
        for (size_t i = 0; i < nodeTrans.size(); i++)
        {
            auto& node = nodeTrans[i];
            const size_t blasIndex = Assets::Scene::DecodeModelIndex(node.modelId);
            if (blasIndex >= rt_->blas.size())
            {
                SPDLOG_WARN("Skipping TLAS instance with stale model index {} (BLAS count {})", blasIndex,
                            rt_->blas.size());
                continue;
            }
            const bool includeInGpuAs =
                (node.visible & Runtime::RenderParticipation::gpuAs) != 0u && !node.excludeFromAS;
            instances.push_back(RayTracing::TopLevelAccelerationStructure::CreateInstance(
                rt_->blas[blasIndex], glm::transpose(node.worldTS), node.instanceId, includeInGpuAs));
        }

        if (instances.size() > rt_->properties->MaxInstanceCount())
        {
            Throw(std::runtime_error(fmt::format("TLAS instance count {} exceeds device limit {}",
                instances.size(), rt_->properties->MaxInstanceCount())));
        }
        if (instances.size() > rt_->tlasInstanceCapacity)
        {
            SPDLOG_INFO("Growing TLAS instance capacity from {} for {} instances",
                        rt_->tlasInstanceCapacity, instances.size());
            CreateAccelerationStructures();
        }

        const int instanceCount = static_cast<int>(instances.size());
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
