#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Rendering/FrameSubmission.hpp"
#include "Engine/Rendering/RenderSubsystems.hpp"
#include "Engine/Vulkan/TracyGpuProfilerBackend.hpp"
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
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
#include "Engine/Runtime/Scene/SceneBuilder.hpp"

#include "Engine/Utilities/Exception.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Options.hpp"
#include "Engine/Rendering/ExternalPassRegistry.hpp"
#include "Engine/Rendering/RendererChoices.hpp"
#include "Engine/Rendering/Preview/RenderViewServices.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"
#include "Engine/Runtime/Profiling/TracyIntegration.hpp"
#include "Engine/Runtime/Interface/UserInterface.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/PipelineCommon/VisibilityBufferLayout.hpp"
#include "Engine/Rendering/PipelineCommon/RestirDI.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include "Engine/Rendering/Interface/IAtmosphereSubsystem.hpp"
#include "Engine/Rendering/Upscaler/IUpscaler.hpp"
#include "Engine/Rendering/Upscaler/UpscalerRegistry.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace
{
    constexpr const char* kPortabilitySubsetExtensionName = "VK_KHR_portability_subset";

    // The bindless arrays are declared at their full count, so the device has to be able to hold
    // every descriptor the profile asks for -- not some round number. The totals come from the
    // profile itself (see Assets::FBindlessProfile), which is also what sizes the layout.
    //
    // The limits queried are the *update-after-bind* ones, not VkPhysicalDeviceLimits. Every
    // binding in this layout carries VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT and the set is
    // created with UPDATE_AFTER_BIND_POOL (DescriptorSystem.cpp), and the spec excludes such
    // bindings from the base maxPerStageDescriptor* limits. Reading the base limits instead
    // demotes every Apple GPU: an M3 Max reports maxPerStageDescriptorSamplers = 16 while happily
    // creating the full 17k-descriptor layout.
    bool CanBackBindlessProfile(const VkPhysicalDeviceDescriptorIndexingProperties& limits,
                                const VkPhysicalDeviceDescriptorIndexingFeatures& features,
                                const Assets::FBindlessProfile& profile)
    {
        const bool hasFeatures = features.runtimeDescriptorArray &&
            features.shaderSampledImageArrayNonUniformIndexing &&
            features.descriptorBindingPartiallyBound &&
            features.descriptorBindingSampledImageUpdateAfterBind &&
            (profile.StorageImages() == 0 ||
             (features.shaderStorageImageArrayNonUniformIndexing &&
              features.descriptorBindingStorageImageUpdateAfterBind));

        const uint32_t combinedImageSamplers = profile.CombinedImageSamplers();
        const uint32_t storageImages = profile.StorageImages();
        return hasFeatures &&
            limits.maxPerStageDescriptorUpdateAfterBindSampledImages >= combinedImageSamplers &&
            limits.maxPerStageDescriptorUpdateAfterBindSamplers >= combinedImageSamplers &&
            limits.maxPerStageDescriptorUpdateAfterBindStorageImages >= storageImages &&
            limits.maxDescriptorSetUpdateAfterBindSampledImages >= combinedImageSamplers &&
            limits.maxDescriptorSetUpdateAfterBindSamplers >= combinedImageSamplers &&
            limits.maxDescriptorSetUpdateAfterBindStorageImages >= storageImages &&
            limits.maxPerStageUpdateAfterBindResources >= combinedImageSamplers + storageImages;
    }

    bool SupportsBCTextures(VkPhysicalDevice physicalDevice)
    {
        // Asked per format and usage rather than inferred from the platform: BC7 is what KTX2/Basis
        // assets transcode to, and the only thing that matters is whether it can be sampled.
        constexpr VkFormatFeatureFlags required =
            VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
        for (const VkFormat format : {VK_FORMAT_BC7_UNORM_BLOCK, VK_FORMAT_BC7_SRGB_BLOCK})
        {
            VkFormatProperties properties = {};
            vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
            if ((properties.optimalTilingFeatures & required) != required)
            {
                return false;
            }
        }
        return true;
    }

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
        struct RendererDescriptor
        {
            ERendererType type;
            const char* name;
            FRendererContract contract;
            uint32_t referenceColumn;
            uint32_t referenceRow;
        };

        std::array<LogicRendererFactory, ERT_Compatibility + 1> RendererFactories{};

        bool ShouldWarmupRenderer(const ERendererType type)
        {
            if (type != ERT_PathTracing)
            {
                return true;
            }

            const NextEngine* engine = NextEngine::GetInstance();
            return engine == nullptr || engine->IsEffectiveSharcEnabled();
        }

        const std::array RendererDescriptors{
            RendererDescriptor{ERT_PathTracing, "PathTracing", {
                                   ESceneResource::Voxel | ESceneResource::Ambient | ESceneResource::TLAS | ESceneResource::SHARC |
                                       ESceneResource::LightGrid,
                                   EViewPrepass::Cull | EViewPrepass::Clear | EViewPrepass::Visibility,
                                   ERenderOutput::Color | ERenderOutput::Depth | ERenderOutput::Motion | ERenderOutput::ObjectId |
                                       ERenderOutput::Normal | ERenderOutput::Albedo | ERenderOutput::Diffuse | ERenderOutput::Specular,
                                   EPostProcess::Temporal | EPostProcess::Upscale |
                                       EPostProcess::RayReconstruction | EPostProcess::FrameGeneration |
                                       EPostProcess::DebugGBuffer,
                                    EHistoryChannel::Diffuse | EHistoryChannel::Specular | EHistoryChannel::Albedo | EHistoryChannel::ObjectId}, 1, 1},
            RendererDescriptor{ERT_PathTracingLite, "PathTracingLite", {
                                   ESceneResource::Voxel | ESceneResource::Ambient | ESceneResource::TLAS | ESceneResource::LightGrid,
                                   EViewPrepass::Cull | EViewPrepass::Clear | EViewPrepass::Visibility,
                                   ERenderOutput::Color | ERenderOutput::Depth | ERenderOutput::Motion | ERenderOutput::ObjectId |
                                       ERenderOutput::Normal | ERenderOutput::Albedo | ERenderOutput::Diffuse | ERenderOutput::Specular,
                                   EPostProcess::Temporal | EPostProcess::Upscale |
                                       EPostProcess::RayReconstruction | EPostProcess::FrameGeneration |
                                       EPostProcess::DebugGBuffer,
                                   EHistoryChannel::Diffuse | EHistoryChannel::Specular | EHistoryChannel::Albedo | EHistoryChannel::ObjectId,
                                    false, true}, 1, 1},
            RendererDescriptor{ERT_SoftwareTracing, "SoftwareTracing", {
                                   ESceneResource::Voxel | ESceneResource::Ambient | ESceneResource::LightGrid,
                                   EViewPrepass::Cull | EViewPrepass::Clear | EViewPrepass::Visibility | EViewPrepass::CSM,
                                   ERenderOutput::Color | ERenderOutput::Depth | ERenderOutput::Motion | ERenderOutput::ObjectId |
                                       ERenderOutput::Normal | ERenderOutput::Albedo | ERenderOutput::Diffuse | ERenderOutput::Specular,
                                   EPostProcess::Temporal | EPostProcess::Upscale |
                                       EPostProcess::DebugGBuffer,
                                   EHistoryChannel::Diffuse | EHistoryChannel::Specular | EHistoryChannel::Albedo | EHistoryChannel::ObjectId,
                                    false, true}, 1, 0},
            RendererDescriptor{ERT_SoftwareModern, "SoftwareModern", {
                                   ESceneResource::Voxel | ESceneResource::Ambient | ESceneResource::LightGrid,
                                   EViewPrepass::Cull | EViewPrepass::Clear | EViewPrepass::Visibility | EViewPrepass::CSM,
                                   ERenderOutput::Color | ERenderOutput::Depth | ERenderOutput::Motion | ERenderOutput::ObjectId |
                                       ERenderOutput::Normal | ERenderOutput::Albedo | ERenderOutput::Diffuse | ERenderOutput::Specular,
                                   EPostProcess::Temporal | EPostProcess::Upscale |
                                       EPostProcess::DebugGBuffer,
                                   EHistoryChannel::Diffuse | EHistoryChannel::Specular | EHistoryChannel::Albedo | EHistoryChannel::ObjectId,
                                    false, true}, 0, 0},
            RendererDescriptor{ERT_VoxelTracing, "VoxelTracing", {
                                   ESceneResource::Voxel | ESceneResource::Ambient,
                                   EViewPrepass::None,
                                   ERenderOutput::Color,
                                   EPostProcess::Upscale,
                                    EHistoryChannel::None, false}, 0, 1},
            RendererDescriptor{ERT_SoftwareModernNoAmbient, "SoftwareModernNoAmbient", {
                                   ESceneResource::LightGrid,
                                   EViewPrepass::Cull | EViewPrepass::Clear | EViewPrepass::Visibility | EViewPrepass::CSM,
                                   ERenderOutput::Color | ERenderOutput::Depth | ERenderOutput::Motion |
                                       ERenderOutput::ObjectId | ERenderOutput::Normal,
                                   EPostProcess::Upscale | EPostProcess::DebugGBuffer,
                                    EHistoryChannel::None, true, true}, 0, 1},
            // Everything None: no scene resources, no prepasses, no outputs. The empty `outputs`
            // set is what tells the base renderer there is no screen-space chain to build or
            // resolve from, so a constrained device never creates resources it cannot back.
            RendererDescriptor{ERT_Compatibility, "Compatibility", {
                                   ESceneResource::None,
                                   EViewPrepass::None,
                                   ERenderOutput::None,
                                   EPostProcess::None,
                                   EHistoryChannel::None, false, false}, 0, 0},
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

    // Why the last ProbeBindlessProfile call chose the compatibility profile. Only SetPhysicalDevice
    // prints it, so the verdict appears once even though the probe runs twice.
    static std::string LastBindlessProfileDecision;

    Assets::FBindlessProfile ProbeBindlessProfile(VkPhysicalDevice physicalDevice)
    {
        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
        indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
        bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddressFeatures.pNext = &indexingFeatures;
        VkPhysicalDeviceFeatures2 features = {};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &bufferDeviceAddressFeatures;
        vkGetPhysicalDeviceFeatures2(physicalDevice, &features);

        VkPhysicalDeviceDescriptorIndexingProperties indexingProperties = {};
        indexingProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES;
        VkPhysicalDeviceProperties2 properties = {};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &indexingProperties;
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

        const bool forceCompatibility = GOption != nullptr && GOption->ForceCompatibilityRenderer;
        // Every renderer except ERT_Compatibility reads the scene through addresses in the GPUScene
        // push constant, so a device without bufferDeviceAddress is just as constrained as one that
        // cannot size the descriptor arrays -- same verdict, different reason.
        const bool hasBufferDeviceAddress = bufferDeviceAddressFeatures.bufferDeviceAddress != VK_FALSE;
        const Assets::FBindlessProfile full = Assets::FBindlessProfile::Full();
        if (!forceCompatibility && hasBufferDeviceAddress &&
            CanBackBindlessProfile(indexingProperties, indexingFeatures, full))
        {
            return full;
        }

        const Assets::FBindlessProfile compatibility = Assets::FBindlessProfile::Compatibility();
        // Recorded rather than logged: this is a pure query, and renderer creation calls it once to
        // pick the renderer set before SetPhysicalDevice calls it again to fill caps_. Logging here
        // would print the verdict twice.
        LastBindlessProfileDecision = fmt::format(
            "GPU '{}' {} the full renderer contract (bufferDeviceAddress {}; update-after-bind "
            "per-stage sampled/sampler/storage {}/{}/{}, per-set {}/{}/{}, resources {}; the "
            "bindless layout needs {} combined image samplers and {} storage images); "
            "using the compatibility profile",
            properties.properties.deviceName,
            forceCompatibility ? "was asked to ignore" : "cannot meet",
            hasBufferDeviceAddress ? "supported" : "MISSING",
            indexingProperties.maxPerStageDescriptorUpdateAfterBindSampledImages,
            indexingProperties.maxPerStageDescriptorUpdateAfterBindSamplers,
            indexingProperties.maxPerStageDescriptorUpdateAfterBindStorageImages,
            indexingProperties.maxDescriptorSetUpdateAfterBindSampledImages,
            indexingProperties.maxDescriptorSetUpdateAfterBindSamplers,
            indexingProperties.maxDescriptorSetUpdateAfterBindStorageImages,
            indexingProperties.maxPerStageUpdateAfterBindResources,
            full.CombinedImageSamplers(), full.StorageImages());

        if (!CanBackBindlessProfile(indexingProperties, indexingFeatures, compatibility))
        {
            Throw(std::runtime_error(fmt::format(
                "GPU '{}' cannot back even the compatibility descriptor layout "
                "({} combined image samplers, {} storage images)",
                properties.properties.deviceName, compatibility.CombinedImageSamplers(),
                compatibility.StorageImages())));
        }
        return compatibility;
    }

    FRendererRequirements GetRendererRequirements(ERendererType type)
    {
        const FRendererContract& contract = GetRendererDescriptor(type).contract;
        return {
            .requestAmbientCube = HasAny(contract.sceneResources, ESceneResource::Ambient),
            .requestLightGrid = HasAny(contract.sceneResources, ESceneResource::LightGrid),
            .requestRayTracing = HasAny(contract.sceneResources, ESceneResource::TLAS),
            .requestVoxelGeometry = HasAny(contract.sceneResources, ESceneResource::Voxel),
        };
    }

    void RegisterLogicRendererFactory(const ERendererType type, LogicRendererFactory factory)
    {
        const size_t index = static_cast<size_t>(type);
        if (index >= RendererFactories.size())
        {
            Throw(std::invalid_argument("logic renderer type is out of range"));
        }
        RendererFactories[index] = factory;
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
        lightGridBuilder_ = std::make_unique<LightGridBuilder>(*this);
        atmosphere_ = Rendering::Atmosphere::CreateRegisteredAtmosphereSubsystem(*this);
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
        ctx_.tracyGpuProfiler.reset();
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

#if GK_TRACY_ENABLED
        if (GOption != nullptr && GOption->HardwareQuery &&
            HasDeviceExtension(physicalDevice, VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME))
        {
            requiredExtensions.push_back(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME);
            tracyCalibratedTimestampsAvailable_ = true;
        }
#endif

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

        // Resolved before any renderer decision below, and before any device resource is created:
        // the profile decides which renderers may run at all, so correcting it afterwards would
        // mean tearing down resources that were already sized for the wrong contract.
        caps_.bindlessProfile = ProbeBindlessProfile(physicalDevice);
        caps_.supportsBCTextures = SupportsBCTextures(physicalDevice);
        if (!HasFullBindlessBudget())
        {
            SPDLOG_WARN("{}", LastBindlessProfileDecision);
        }

        caps_.supportRayTracing = !GOption->ForceNoRT && HasFullBindlessBudget() &&
            ctx_.instance->SupportsRayQuery(physicalDevice);
        if (caps_.supportRayTracing)
        {
            rt_ = std::make_unique<RayTracingResources>();
        }

        // One policy, shared with NextEngine::Start and the editor's renderer picker, rather than
        // a second hand-rolled fallback chain here.
        const ERendererType resolvedRendererType =
            Rendering::ResolveRendererChoice(logicRenderers_.current, RendererChoiceCapabilities());
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

        ctx_.globalTexturePool.reset(new Assets::GlobalTexturePool(
            *ctx_.device, *ctx_.commandPool2, *ctx_.commandPool, caps_.bindlessProfile,
            caps_.supportsBCTextures));

        // The compatibility profile renders no scene, so the subsystems that only exist to feed one
        // are dropped here instead of being guarded at each of their use sites. Both are held in
        // optional pointers that every caller already null-checks.
        if (!HasFullBindlessBudget())
        {
            atmosphere_.reset();
            upscaler_.reset();
        }

        OnDeviceSet();
        if (atmosphere_)
        {
            atmosphere_->CreateDeviceResources();
        }
        CreateSwapChain();
        // Keep hidden windows hidden (agent validation captures, unit-test engine fixture), and
        // let normal startup show only after its first presentable frame is ready.
        if (!ctx_.window->Config().HiddenWindow && !ctx_.window->Config().DeferShowUntilFirstPresent)
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

    void VulkanBaseRenderer::BeginPipelineWarmup()
    {
        if (!ctx_.device || !frame_.swapChain || pipelineWarmup_.active)
        {
            return;
        }

        pipelineWarmup_.active = true;
        pipelineWarmup_.completedRenderers = 0;
        const auto warmupRendererCount = std::count_if(
            logicRenderers_.registeredTypes.begin(), logicRenderers_.registeredTypes.end(), ShouldWarmupRenderer);
        pipelineWarmup_.totalRenderers = static_cast<uint32_t>(warmupRendererCount) +
            (frame_.sceneChainInitializationPending ? 1u : 0u) + (upscaler_ != nullptr ? 1u : 0u);
        pipelineWarmup_.pipelinesCreatedBeforeWarmup = ctx_.device->PipelineCreationCount();
        pipelineWarmup_.completedPipelines = 0;
        pipelineWarmup_.currentRenderer = frame_.sceneChainInitializationPending
            ? "Core scene pipelines"
            : "Preparing Vulkan shader cache";
        nextWarmupRenderer_ = 0;
        warmupUpscalersPending_ = upscaler_ != nullptr;
        warmupOriginalWindowTitle_ = ctx_.window->GetTitle();
        SPDLOG_INFO("Vulkan pipeline warm-up: preparing {} pipeline groups", pipelineWarmup_.totalRenderers);
    }

    bool VulkanBaseRenderer::PumpPipelineWarmup()
    {
        if (!pipelineWarmup_.active)
        {
            return true;
        }

        if (frame_.sceneChainInitializationPending)
        {
            pipelineWarmup_.currentRenderer = "Core scene pipelines";
            const uint32_t next = pipelineWarmup_.completedRenderers + 1;
            ctx_.window->SetTitle(fmt::format("{} - Optimizing shaders ({}/{})", warmupOriginalWindowTitle_, next,
                                              pipelineWarmup_.totalRenderers));

            const auto start = std::chrono::steady_clock::now();
            CreateDeferredSceneSwapChainResources();
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
            pipelineWarmup_.completedRenderers = next;
            pipelineWarmup_.completedPipelines =
                ctx_.device->PipelineCreationCount() - pipelineWarmup_.pipelinesCreatedBeforeWarmup;
            SPDLOG_INFO("Vulkan pipeline warm-up [{}/{}] {}: {:.1f} ms ({} pipelines warmed, {} before warm-up)", next,
                        pipelineWarmup_.totalRenderers, pipelineWarmup_.currentRenderer, elapsed.count(),
                        pipelineWarmup_.completedPipelines, pipelineWarmup_.pipelinesCreatedBeforeWarmup);
            return false;
        }

        while (nextWarmupRenderer_ < logicRenderers_.registeredTypes.size())
        {
            const ERendererType type = logicRenderers_.registeredTypes[nextWarmupRenderer_++];
            if (!ShouldWarmupRenderer(type))
            {
                SPDLOG_INFO("Vulkan pipeline warm-up: skipped {} (feature disabled)", GetRendererName(type));
                continue;
            }

            pipelineWarmup_.currentRenderer = GetRendererName(type);
            const uint32_t next = pipelineWarmup_.completedRenderers + 1;
            ctx_.window->SetTitle(fmt::format("{} - Optimizing shaders ({}/{})", warmupOriginalWindowTitle_, next,
                                              pipelineWarmup_.totalRenderers));

            const auto start = std::chrono::steady_clock::now();
            LogicRendererBase* renderer = EnsureLogicRenderer(type);
            if (renderer)
            {
                renderer->WarmupPipelines();
            }
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
            pipelineWarmup_.completedRenderers = next;
            pipelineWarmup_.completedPipelines =
                ctx_.device->PipelineCreationCount() - pipelineWarmup_.pipelinesCreatedBeforeWarmup;
            SPDLOG_INFO("Vulkan pipeline warm-up [{}/{}] {}: {:.1f} ms ({} pipelines warmed, {} before warm-up)", next,
                        pipelineWarmup_.totalRenderers, pipelineWarmup_.currentRenderer, elapsed.count(),
                        pipelineWarmup_.completedPipelines, pipelineWarmup_.pipelinesCreatedBeforeWarmup);
            return false;
        }

        // Each registered provider must compile even when another upscaler is currently selected.
        // Keep their history images lazy: the cache only needs pipeline compilation, not a second
        // screen-sized temporal history allocation at startup.
        if (warmupUpscalersPending_)
        {
            pipelineWarmup_.currentRenderer = "Upscaler providers";
            ctx_.window->SetTitle(fmt::format("{} - Optimizing shaders (upscalers)", warmupOriginalWindowTitle_));
            const auto start = std::chrono::steady_clock::now();
            upscaler_->WarmupPipelines();
            const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
            pipelineWarmup_.completedPipelines =
                ctx_.device->PipelineCreationCount() - pipelineWarmup_.pipelinesCreatedBeforeWarmup;
            SPDLOG_INFO("Vulkan pipeline warm-up [upscalers] {:.1f} ms ({} pipelines warmed, {} before warm-up)",
                        elapsed.count(), pipelineWarmup_.completedPipelines,
                        pipelineWarmup_.pipelinesCreatedBeforeWarmup);
            warmupUpscalersPending_ = false;
            return false;
        }

        pipelineWarmup_.active = false;
        pipelineWarmup_.currentRenderer.clear();
        deferSceneChainForStartup_ = false;
        ctx_.device->PersistPipelineCache();
        ctx_.window->SetTitle(warmupOriginalWindowTitle_);
        SPDLOG_INFO("Vulkan pipeline warm-up: complete ({} pipeline groups, {} pipelines warmed, {} before warm-up)",
                    pipelineWarmup_.totalRenderers, pipelineWarmup_.completedPipelines,
                    pipelineWarmup_.pipelinesCreatedBeforeWarmup);
        return true;
    }

    void VulkanBaseRenderer::WarmupAllPipelines()
    {
        BeginPipelineWarmup();
        while (!PumpPipelineWarmup())
        {
        }
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
        ctx_.tracyGpuProfiler.reset();
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

    Rendering::FRendererChoiceCapabilities VulkanBaseRenderer::RendererChoiceCapabilities() const
    {
        return {
            .supportsRayTracing = caps_.supportRayTracing,
            .hasFullAmbientCubeBudget = caps_.fullAmbientCubeBudget,
            .hasFullBindlessBudget = HasFullBindlessBudget(),
        };
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

    bool VulkanBaseRenderer::IsCheckerboardRenderingActive() const
    {
        // Checkerboard is a shading *rate*, and a rate is only safe once something else guarantees
        // dense surface data. That something is Core.SurfaceBuild, so checkerboard requires the
        // Primary Surface path: renderers without a Build pass (PathTracing, PathTracingLite,
        // VoxelTracing) simply shade at full rate.
        return IsSurfacePathActive() &&
            frameSettings_.userSettings.CheckerboardRendering &&
            temporalSuperResolutionActive_ && ActiveViewBankBase() == 0u &&
            !frameSettings_.progressiveRendering &&
            !frameSettings_.offlineProgressivePathTracing &&
            (!GOption || !GOption->ReferenceMode);
    }

    bool VulkanBaseRenderer::IsReferenceViewAccumulationActive() const
    {
        // Transient views are complete after their single scheduled render. In particular, editor
        // thumbnails must not enter reference-mode progressive accumulation just because they use
        // a secondary RT bank.
        return GOption != nullptr && GOption->ReferenceMode && !ActiveRenderView().IsPrimary() &&
            ActiveRenderView().Schedule() != EViewSchedule::Transient;
    }

    uint32_t VulkanBaseRenderer::ProgressiveSampleCountForActiveView() const
    {
        return IsReferenceViewAccumulationActive()
            ? ActiveRenderView().State().progressiveFrame
            : frameSettings_.progressiveAccumulatedFrames;
    }

    uint32_t VulkanBaseRenderer::CheckerboardDispatchWidth(
        const uint32_t width, const Assets::GPUScene& gpuScene) const
    {
        return PipelineCommon::GetCheckerboardDispatchWidth(
            width, (gpuScene.CustomData2 & PipelineCommon::checkerboardShadingFlag) != 0u);
    }

    void VulkanBaseRenderer::ConfigureCheckerboardShading(
        Assets::GPUScene& gpuScene, const bool allowed) const
    {
        gpuScene.CustomData2 &= ~PipelineCommon::checkerboardShadingFlag;
        if (allowed && IsCheckerboardRenderingActive())
        {
            gpuScene.CustomData2 |= PipelineCommon::checkerboardShadingFlag;
        }
    }

    bool VulkanBaseRenderer::IsSurfacePathActive() const
    {
        return GetRendererContract(logicRenderers_.current).usesPrimarySurface;
    }

    bool VulkanBaseRenderer::IsSparseCheckerboardLightingActive() const
    {
        if (!IsCheckerboardRenderingActive())
        {
            return false;
        }
        // Only Native TAAU can consume a sparse image; every external upscaler contract requires a
        // dense scene color, depth and motion triple.
        if (activeUpscalerType_ != Rendering::Upscaler::EUpscalerType::NativeTAAU)
        {
            return false;
        }
        // A module pass that paints the scene color after the primary view writes every pixel and
        // would turn the unshaded parity into plausible-looking garbage the upscaler cannot detect.
        return !HasScenePassAfterPrimaryView();
    }

    bool VulkanBaseRenderer::HasScenePassAfterPrimaryView() const
    {
        const uint32_t availableOutputs =
            static_cast<uint32_t>(GetRendererContract(logicRenderers_.current).outputs);
        for (const auto& externalPass : overlay_.externalPasses)
        {
            const FExternalPassContract contract = externalPass->Contract();
            if (externalPass->PaintsWholeSceneThisFrame() &&
                contract.insertionPoint == EExternalPassInsertionPoint::AfterPrimaryView &&
                contract.scope == EExternalPassScope::PrimaryView &&
                AreExternalPassInputsAvailable(contract, availableOutputs))
            {
                return true;
            }
        }
        return false;
    }

    void VulkanBaseRenderer::ResolveCheckerboardShading(
        VkCommandBuffer commandBuffer,
        const Assets::GPUScene& gpuScene,
        const PipelineCommon::ECheckerboardResolveSet resolveSet)
    {
        if ((gpuScene.CustomData2 & PipelineCommon::checkerboardShadingFlag) == 0u ||
            !overlay_.checkerboardResolvePipeline)
        {
            return;
        }
        // Sparse mode is exactly "do not run this pass": Native TAAU consumes the shaded parity and
        // fills the rest from history, which is strictly better than a horizontal neighbour copy.
        if (IsSparseCheckerboardLightingActive())
        {
            return;
        }

        constexpr auto compute = PipelineCommon::ERenderStage::Compute;
        constexpr auto readWrite = PipelineCommon::EResourceAccess::ShaderRead |
            PipelineCommon::EResourceAccess::ShaderWrite;
        switch (resolveSet)
        {
        case PipelineCommon::ECheckerboardResolveSet::Tracing:
            TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SINGLE_DIFFUSE, compute, readWrite},
                {Assets::Bindless::RT_SINGLE_SPECULAR, compute, readWrite},
                {Assets::Bindless::RT_DIFFUSE_HITDIST, compute, readWrite},
                {Assets::Bindless::RT_SPECULAR_HITDIST, compute, readWrite},
                // RT_ALBEDO is the compose demodulation guide, and Core overrides it to white at
                // sky and emissive pixels, so it is a lighting channel here rather than a surface
                // one. Every other surface RT stays out of the resolve.
                {Assets::Bindless::RT_ALBEDO, compute, readWrite},
            }, "checkerboard tracing lighting resolve");
            break;
        case PipelineCommon::ECheckerboardResolveSet::SoftwareModernNoAmbient:
            TransitionActiveViewImages(commandBuffer, {
                {Assets::Bindless::RT_SINGLE_DIFFUSE, compute, readWrite},
                {Assets::Bindless::RT_AMBIENT, compute, readWrite},
            }, "checkerboard software modern no ambient lighting resolve");
            break;
        }

        SCOPED_GPU_TIMER("checkerboard resolve");
        Assets::GPUScene resolveScene = gpuScene;
        resolveScene.CustomData1 = static_cast<uint32_t>(resolveSet);
        overlay_.checkerboardResolvePipeline->BindPipeline(commandBuffer, resolveScene);
        const VkExtent2D extent = ActiveViewRenderExtent();
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount((extent.width + 1u) / 2u, 8),
                      Utilities::Math::GetSafeDispatchCount(extent.height, 8), 1);
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
        // Only the full renderer needs addresses; the compatibility profile binds its buffers
        // through descriptors precisely because devices like the A12X cannot provide them.
        if (HasFullBindlessBudget() && deviceProperties.apiVersion < VK_API_VERSION_1_2 &&
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

        // Optional heatmap instrumentation. Software / translation ICDs (lvp, dozen) do not
        // always expose these, so the chain only picks them up when the device has both.
        VkPhysicalDeviceShaderClockFeaturesKHR shaderClockFeatures = {};
#if WIN32 && GK_ENABLE_SHADER_CLOCK
        if (HasDeviceExtension(physicalDevice, VK_KHR_SHADER_CLOCK_EXTENSION_NAME) &&
            HasDeviceExtension(physicalDevice, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME))
        {
            requiredExtensions.insert(requiredExtensions.end(),
                                      {
                                          VK_KHR_SHADER_CLOCK_EXTENSION_NAME,
                                          VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME
                                      });

            // Opt-in into mandatory device features.
            shaderClockFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_CLOCK_FEATURES_KHR;
            shaderClockFeatures.pNext = nextDeviceFeatures;
            shaderClockFeatures.shaderSubgroupClock = true;
            nextDeviceFeatures = &shaderClockFeatures;
        }
        else
        {
            SPDLOG_INFO("Shader clock heatmap instrumentation unavailable on this device");
        }
#endif

        // support bindless material
        VkPhysicalDeviceDescriptorIndexingFeatures indexingFeatures = {};
        indexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        indexingFeatures.pNext = nextDeviceFeatures;
        indexingFeatures.runtimeDescriptorArray = supportedIndexingFeatures.runtimeDescriptorArray;
        indexingFeatures.shaderSampledImageArrayNonUniformIndexing = supportedIndexingFeatures.shaderSampledImageArrayNonUniformIndexing;
        indexingFeatures.shaderStorageImageArrayNonUniformIndexing = supportedIndexingFeatures.shaderStorageImageArrayNonUniformIndexing;
        indexingFeatures.descriptorBindingPartiallyBound = supportedIndexingFeatures.descriptorBindingPartiallyBound;
        indexingFeatures.descriptorBindingSampledImageUpdateAfterBind = supportedIndexingFeatures.descriptorBindingSampledImageUpdateAfterBind;
        indexingFeatures.descriptorBindingStorageImageUpdateAfterBind = supportedIndexingFeatures.descriptorBindingStorageImageUpdateAfterBind;
        indexingFeatures.descriptorBindingVariableDescriptorCount = supportedIndexingFeatures.descriptorBindingVariableDescriptorCount;


        VkPhysicalDeviceBufferDeviceAddressFeatures bufferDeviceAddressFeatures = {};
        bufferDeviceAddressFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES;
        bufferDeviceAddressFeatures.pNext = &indexingFeatures;
        // Left off under the compatibility profile even where the device offers it, so that forcing
        // the profile on a capable GPU reproduces the constrained device exactly -- buffers lose the
        // address usage, GetDeviceAddress reports null, and any code that quietly depended on an
        // address fails here rather than only on the hardware nobody can debug on.
        bufferDeviceAddressFeatures.bufferDeviceAddress =
            HasFullBindlessBudget() ? supportedBufferDeviceAddressFeatures.bufferDeviceAddress : VK_FALSE;

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

#if GK_TRACY_ENABLED
        ctx_.tracyGpuProfiler = std::make_unique<Vulkan::TracyGpuProfilerBackend>(
            ctx_.instance->Handle(), *ctx_.device, *ctx_.commandPool, tracyCalibratedTimestampsAvailable_);
#endif

#if defined(NDEBUG)
        constexpr const char* buildConfig = "release";
#else
        constexpr const char* buildConfig = "debug";
#endif
        GkProfiling::AppInfo(fmt::format(
            "target=gkNextRenderer;renderer={};gpu={};config={}",
            GetRendererName(logicRenderers_.current), ctx_.device->DeviceProperties().deviceName, buildConfig));
    }

    void VulkanBaseRenderer::OnDeviceSet()
    {
        if (upscaler_)
        {
            Rendering::Upscaler::FDeviceInfo deviceInfo{};
            deviceInfo.device = ctx_.device->Handle();
            deviceInfo.pipelineCache = ctx_.device->PipelineCache();
            deviceInfo.onPipelineCreated = [this](const char* label)
            {
                ctx_.device->RecordPipelineCreated(label);
            };
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

        // Progressive history belongs to the view bank. Rebuilding or reusing a bank must not
        // carry a previous view's running average into the new view.
        bindless_.images[bankBase + Assets::Bindless::RT_PROGRESSIVE_SCENE_COLOR].reset();

        CREATE_STORAGE_IMAGE(RT_SINGLE_DIFFUSE, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        CREATE_STORAGE_IMAGE(RT_VISIBILITY_INSTANCE, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_VISIBILITY_TRIANGLE, VK_FORMAT_R16_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        CREATE_STORAGE_IMAGE(RT_VISIBILITY_INSTANCE_DRAW, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT );
        CREATE_STORAGE_IMAGE(RT_VISIBILITY_TRIANGLE_DRAW, VK_FORMAT_R16_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT );
        CREATE_STORAGE_IMAGE(RT_OBJECTID_0, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT );
        CREATE_STORAGE_IMAGE(RT_OBJECTID_1, VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT );
        // External temporal upscalers bind motion vectors as sampled images during their
        // current-frame dispatch. Keep STORAGE for engine writes and SAMPLED for the SDK read.
        CREATE_STORAGE_IMAGE(RT_MOTIONVECTOR, VK_FORMAT_R32G32_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT );
        CREATE_STORAGE_IMAGE(RT_ALBEDO, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_NORMAL, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
        CREATE_STORAGE_IMAGE(RT_SHADER_TIMER, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT );
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
        const uint32_t bindlessIdx = ActiveViewBankBase() + Assets::Bindless::RT_PROGRESSIVE_SCENE_COLOR;
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

    void VulkanBaseRenderer::CreateScreenShotBuffer()
    {
        const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(frame_.swapChain->Extent().width) *
                                        frame_.swapChain->Extent().height *
                                        (frame_.swapChain->Format() == VK_FORMAT_R16G16B16A16_SFLOAT ? 8 : 4);
        screenshot_.buffer.reset(new Buffer(*ctx_.device, bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT));

        uint32_t memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        try
        {
            screenshot_.bufferMemory.reset(new DeviceMemory(*ctx_.device, *screenshot_.buffer, memProps));
        }
        catch (...)
        {
            memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            screenshot_.bufferMemory.reset(new DeviceMemory(*ctx_.device, *screenshot_.buffer, memProps));
        }

        ctx_.device->DebugUtils().SetObjectName(screenshot_.buffer->Handle(), "Screenshot Buffer");
        screenshot_.bufferMemory->SetName("Screenshot Memory");

        screenshot_.captureRequested = false;
        screenshot_.captureReady = false;
        screenshot_.initialized = false;
        screenshot_.captureSubmitSerial = 0;
    }

    void VulkanBaseRenderer::CreateRenderImages()
    {
        bindless_.images.resize(Assets::Bindless::RT_COUNT);

        // Primary view RT bank (bank 0 == legacy absolute layout).
        CreateRenderTargetBank(0);
        renderViews_->ResetSwapChainResources();
        renderViews_->InvalidateAllTemporalHistory(EHistoryInvalidationReason::SwapchainRecreated);
        // Non-primary view resources were destroyed with the swapchain; recreate on demand.
        if (renderViewServices_)
        {
            renderViewServices_->OnSwapChainResourcesInvalidated(/*releaseOffscreenSampledOutputs*/ false);
        }
        lateToneMapping_.inputImage.reset();
        lateToneMapping_.outputImage.reset();
        lateToneMapping_.inputInitialized = false;
        lateToneMapping_.outputInitialized = false;
        {
            lateToneMapping_.inputImage = std::make_unique<RenderImage>(
                Device(), frame_.swapChain->OutputExtent(), VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                false, "Late tone-mapping input");
            ctx_.globalTexturePool->BindStorageTexture(
                Assets::Bindless::RT_TONEMAP_INPUT, lateToneMapping_.inputImage->GetImageView());

            // Unlike the input (which the upscaler and post filter write at view-local pixels),
            // the tone-mapping output is addressed in swapchain pixels: every composed view writes
            // it at its own output offset and blits that same rect out. It therefore has to cover
            // the whole image, not just the rect the primary view happens to present into.
            lateToneMapping_.outputImage = std::make_unique<RenderImage>(
                Device(), frame_.swapChain->Extent(), VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                false, "Late tone-mapping output");
            ctx_.globalTexturePool->BindStorageTexture(
                Assets::Bindless::RT_TONEMAP_OUTPUT, lateToneMapping_.outputImage->GetImageView());
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
                // HUD-less color is a copy of the presented swapchain image, so it is sized in
                // swapchain pixels rather than by the rect the scene presents into.
                frameGeneration_.hudlessImages.emplace_back(std::make_unique<RenderImage>(
                    Device(),
                    frame_.swapChain->Extent(),
                    frame_.swapChain->Format(),
                    VK_IMAGE_TILING_OPTIMAL,
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                    false,
                    debugName.c_str()));
            }
        }

        temporalPostFilter_.pingImage.reset();
        temporalPostFilter_.pongImage.reset();
        temporalPostFilter_.pingInitialized = false;
        temporalPostFilter_.pongInitialized = false;
        const bool temporalPostFilterActive = temporalSuperResolutionActive_ &&
            Rendering::Upscaler::GetUpscalerTypeInfo(
                static_cast<uint32_t>(activeUpscalerType_)).supportsTemporalPostFilter;
        if (temporalPostFilterActive)
        {
            temporalPostFilter_.pingImage = std::make_unique<RenderImage>(
                Device(), frame_.swapChain->OutputExtent(), VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "Temporal post-filter ping");
            ctx_.globalTexturePool->BindStorageTexture(
                Assets::Bindless::RT_TEMPORAL_POST_PING, temporalPostFilter_.pingImage->GetImageView());

            temporalPostFilter_.pongImage = std::make_unique<RenderImage>(
                Device(), frame_.swapChain->OutputExtent(), VK_FORMAT_R16G16B16A16_SFLOAT,
                VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_STORAGE_BIT, false, "Temporal post-filter pong");
            ctx_.globalTexturePool->BindStorageTexture(
                Assets::Bindless::RT_TEMPORAL_POST_PONG, temporalPostFilter_.pongImage->GetImageView());
        }
    }

    void VulkanBaseRenderer::CreateSceneSwapChainResources()
    {
        if (atmosphere_)
        {
            atmosphere_->CreateSwapChainPipelines();
        }
        overlay_.wireframePipeline.reset(new class PipelineCommon::GraphicsPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene(), true, true));
        // X-ray variant: identical except it ignores scene depth, so interior and back-facing
        // edges stay visible. Shares the render pass layout, so both can use the same framebuffers.
        overlay_.wireframeXrayPipeline.reset(new class PipelineCommon::GraphicsPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene(), true, false));
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
                SwapChain(), "assets/shaders/Process.TemporalPostFilter.comp.slang.spv", 64));
        }
        overlay_.toneMappingPipeline.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(
            SwapChain(), "assets/shaders/Process.TonemapAfterUpscaler.comp.slang.spv", 52));
        overlay_.bufferClearPipeline.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(*frame_.swapChain, "assets/shaders/Util.BufferClear.comp.slang.spv", 4));
        overlay_.checkerboardResolvePipeline.reset(new PipelineCommon::ZeroBindPipeline(
            *frame_.swapChain, "assets/shaders/Process.CheckerboardResolve.comp.slang.spv", GetScene()));
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
        overlay_.lightGridBuildPipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Task.LightGridBuild.comp.slang.spv", GetScene()));
        overlay_.shadowGpuCullCompactPipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, shadowGpuCullSpv, GetScene()));
        skin_.pipeline.reset(new PipelineCommon::ZeroBindPipeline(*frame_.swapChain, "assets/shaders/Task.Skinning.comp.slang.spv", GetScene()));
        overlay_.visualDebuggerPipeline.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(*frame_.swapChain, "assets/shaders/Util.VisualDebugger.comp.slang.spv", 20));

        for (const FExternalPassFactory& factory : ExternalPassFactories())
        {
            overlay_.externalPasses.push_back(factory(*this));
            overlay_.externalPasses.back()->CreateResources();
        }

        overlay_.visibilityPipeline.reset(new PipelineCommon::VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));
        const std::array<const ImageView*, PipelineCommon::kVisibilityPlanes.size()> visibilityViews = {
            &GetViewStorageImage(PipelineCommon::kVisibilityPlanes[0].drawSlot)->GetImageView(),
            &GetViewStorageImage(PipelineCommon::kVisibilityPlanes[1].drawSlot)->GetImageView(),
        };
        overlay_.visibilityFrameBuffer.reset(new FrameBuffer(
            frame_.swapChain->RenderExtent(), visibilityViews, overlay_.visibilityPipeline->RenderPass()));

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

    void VulkanBaseRenderer::CreateDeferredSceneSwapChainResources()
    {
        if (!frame_.sceneChainInitializationPending || !frame_.swapChain)
        {
            return;
        }

        // Activating a native upscaler can itself create compute pipelines. Do it here rather
        // than while constructing the first presentable UI frame.
        if (upscaler_)
        {
            upscaler_->SetActiveType(temporalSuperResolutionActive_
                ? activeUpscalerType_
                : Rendering::Upscaler::EUpscalerType::None);
        }

        CreateRenderImages();
        renderViews_->CreateSwapChain(SwapChain());
        CreateSceneSwapChainResources();
        frame_.sceneChainCreated = true;
        frame_.sceneChainInitializationPending = false;
        deferSceneChainForStartup_ = false;
    }

    void VulkanBaseRenderer::CreateSwapChain()
    {
        const auto createSwapChainStart = std::chrono::steady_clock::now();

        // SwapChain
        auto* engine = NextEngine::GetInstance();
        frameSettings_.userSettings = engine->GetUserSettings();
        frameSettings_.progressiveRendering = engine->IsProgressiveRendering();
        frameSettings_.offlineProgressivePathTracing = engine->IsOfflineProgressivePathTracing();
        frameSettings_.progressiveAccumulatedFrames = engine->GetProgressiveRenderAccumulatedFrames();
        frameSettings_.progressiveTargetFrames = engine->GetProgressiveRenderTargetFrames();
        const auto& settings = frameSettings_.userSettings;
        const auto configuredUpscalerType = Rendering::Upscaler::GetUpscalerTypeInfo(
            static_cast<uint32_t>(settings.UpscalerType)).type;
        const bool agentValidationExternalUpscaler =
            GOption->AgentValidation &&
            (configuredUpscalerType == Rendering::Upscaler::EUpscalerType::DLSS ||
             configuredUpscalerType == Rendering::Upscaler::EUpscalerType::DLSSRayReconstruction ||
             configuredUpscalerType == Rendering::Upscaler::EUpscalerType::FidelityFXFSR);
        const auto requestedUpscalerType = agentValidationExternalUpscaler
            ? Rendering::Upscaler::EUpscalerType::NativeTAAU
            : configuredUpscalerType;
        if (agentValidationExternalUpscaler)
        {
            SPDLOG_INFO(
                "Agent validation replaces unavailable {} with Native TAAU; temporal upscaling remains enabled",
                Rendering::Upscaler::GetUpscalerTypeInfo(
                    static_cast<uint32_t>(configuredUpscalerType)).name);
        }
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

        // Everything below sizes the screen-space chain against the presented scene rect, which is
        // the whole image unless a host (the editor) reserves part of the window for its panels.
        const VkRect2D sceneViewport = ResolveSceneViewportRect();
        sceneViewport_.built = sceneViewport.extent;
        sceneViewport_.lastResolved = sceneViewport;
        sceneViewport_.unsettledFrames = 0;
        VkExtent2D renderExtent = sceneViewport.extent;
        activeUpscalerType_ = Rendering::Upscaler::EUpscalerType::None;
        temporalSuperResolutionActive_ = false;
        effectiveSuperResolutionMode_ =
            static_cast<uint32_t>(Rendering::Upscaler::EUpscaleMode::Native);
        if (!GOption->ReferenceMode)
        {
            const auto resolvedMode = Rendering::Upscaler::ResolveUpscaleMode(
                settings.SuperResolution,
                sceneViewport.extent);
            effectiveSuperResolutionMode_ = resolvedMode.mode;

            const FRendererContract& contract = GetRendererContract(logicRenderers_.current);
            const auto& typeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
                static_cast<uint32_t>(requestedUpscalerType));
            const bool hasRequiredOutputs = !typeInfo.requiresDepthAndMotion ||
                HasAll(contract.outputs, ERenderOutput::Depth | ERenderOutput::Motion);
            const bool supportsRequestedPostProcess = HasAny(contract.post, EPostProcess::Upscale) &&
                (!typeInfo.requiresRayReconstruction ||
                 HasAny(contract.post, EPostProcess::RayReconstruction));
            // Temporal upscalers always resolve into the engine-owned late tone-mapping
            // image, not the swapchain image. This keeps them available when a surface
            // needs the intermediate-target + blit presentation fallback.
            temporalSuperResolutionActive_ =
                resolvedMode.enabled && requestedUpscalerType != Rendering::Upscaler::EUpscalerType::None &&
                upscaler_ && SupportsUpscaler(requestedUpscalerType) && hasRequiredOutputs &&
                supportsRequestedPostProcess;

            if (temporalSuperResolutionActive_)
            {
                upscalerFallbackReported_ = false;
                activeUpscalerType_ = requestedUpscalerType;
                if (!deferSceneChainForStartup_)
                {
                    upscaler_->SetActiveType(activeUpscalerType_);
                }
                const auto optimal = upscaler_->GetOptimalRenderSettings(
                    effectiveSuperResolutionMode_,
                    sceneViewport.extent,
                    true,
                    frame_.swapChain->IsHDR(),
                    activeUpscalerType_);
                renderExtent = optimal.renderExtent;
                SPDLOG_INFO("{} active for {}: {}x{} -> {}x{}",
                            typeInfo.name,
                            GetRendererName(logicRenderers_.current),
                            renderExtent.width, renderExtent.height,
                            sceneViewport.extent.width,
                            sceneViewport.extent.height);
            }
            else if (requestedUpscalerType != Rendering::Upscaler::EUpscalerType::None &&
                     (!upscalerFallbackReported_ ||
                      upscalerFallbackReportedType_ != requestedUpscalerType ||
                      upscalerFallbackReportedRenderer_ != logicRenderers_.current))
            {
                upscalerFallbackReported_ = true;
                upscalerFallbackReportedType_ = requestedUpscalerType;
                upscalerFallbackReportedRenderer_ = logicRenderers_.current;
                // A contract without the required outputs is a design fact about the renderer, not a
                // fault; anything else means the upscaler was expected to run and did not.
                if (!hasRequiredOutputs || !supportsRequestedPostProcess)
                {
                    SPDLOG_INFO("{} does not apply to {}: the renderer's contract lacks the required "
                                "outputs or post-process stage; using native rendering",
                                typeInfo.name, GetRendererName(logicRenderers_.current));
                }
                else
                {
                    SPDLOG_WARN("{} is unavailable for {}; using native rendering",
                                typeInfo.name, GetRendererName(logicRenderers_.current));
                }
            }
        }

        if (upscaler_ && !temporalSuperResolutionActive_ && !deferSceneChainForStartup_)
        {
            upscaler_->SetActiveType(Rendering::Upscaler::EUpscalerType::None);
        }

        frame_.swapChain->UpdateRenderViewport(0, 0, renderExtent.width, renderExtent.height);
        frame_.swapChain->UpdateOutputViewport(sceneViewport.offset.x, sceneViewport.offset.y,
                                               sceneViewport.extent.width, sceneViewport.extent.height);

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

        // Screenshots copy from the presented swapchain image, so they work under any renderer and
        // the staging buffer is created before the scene-only resources below.
        CreateScreenShotBuffer();

        // Shared render images. A renderer without outputs presents into the swapchain image
        // directly, so the whole screen-space chain -- RT banks, visibility, the shared compute
        // pipelines -- is neither created nor needed. On a compatibility device it could not be
        // created at all: every one of those pipelines binds the full bindless set.
        //
        // The one place the policy is consulted; from here on the frame path reads the flag.
        frame_.sceneChainInitializationPending =
            deferSceneChainForStartup_ && RendererDeclaresOutputs();
        frame_.sceneChainCreated = RendererDeclaresOutputs() && !frame_.sceneChainInitializationPending;
        if (frame_.sceneChainCreated)
        {
            CreateRenderImages();
            renderViews_->CreateSwapChain(SwapChain());
            CreateSceneSwapChainResources();
        }

        // Delegate
        if (delegates_.createSwapChain)
        {
            delegates_.createSwapChain();
        }

        SPDLOG_INFO("CreateSwapChain took {:.2f}ms",
                    std::chrono::duration<float, std::milli>(
                        std::chrono::steady_clock::now() - createSwapChainStart).count());
    }

    void VulkanBaseRenderer::RefreshSceneSwapChainResources()
    {
        // A renderer without outputs never built the screen-space chain, so a scene swap has
        // nothing to rebuild here.
        if (!frame_.swapChain || !frame_.sceneChainCreated)
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
        overlay_.wireframeXrayPipeline.reset();
        overlay_.wireframePipeline.reset();
        overlay_.bufferClearPipeline.reset();
        overlay_.checkerboardResolvePipeline.reset();
        ambient_.softBake.reset();
        ambient_.clearCache.reset();
        if (rt_)
        {
            rt_->directLightGenPipeline.reset();
        }
        overlay_.gpuCullCompactPipeline.reset();
        overlay_.softMeshShaderFinalizePipeline.reset();
        overlay_.softMeshShaderExpandPipeline.reset();
        overlay_.lightGridBuildPipeline.reset();
        overlay_.shadowGpuCullCompactPipeline.reset();
        skin_.pipeline.reset();
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

        // No compatibility branch: every step below is a reset/clear of something that a renderer
        // without outputs simply never created, and the two subsystem calls are null-guarded.
        renderViews_->ClearSchedule();
        visibilityStateTracker_.Reset();
        swapchainStateTracker_.Reset();
        auxiliaryImageStateTracker_.Reset();
        shadowCameraFamilyCache_ = {};
        if (atmosphere_)
        {
            atmosphere_->DestroySwapChainPipelines();
        }

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
        
        screenshot_.buffer.reset();
        screenshot_.bufferMemory.reset();
        screenshot_.captureRequested = false;
        screenshot_.captureReady = false;
        screenshot_.initialized = false;
        screenshot_.captureSubmitSerial = 0;
        frameGeneration_.hudlessImages.clear();
        temporalPostFilter_.pingImage.reset();
        temporalPostFilter_.pongImage.reset();
        temporalPostFilter_.pingInitialized = false;
        temporalPostFilter_.pongInitialized = false;
        lateToneMapping_.inputImage.reset();
        lateToneMapping_.outputImage.reset();
        lateToneMapping_.inputInitialized = false;
        lateToneMapping_.outputInitialized = false;
        frame_.commandBuffers.reset();
        overlay_.wireframeFrameBuffers.clear();
        overlay_.wireframeXrayPipeline.reset();
        overlay_.wireframePipeline.reset();
        overlay_.bufferClearPipeline.reset();
        overlay_.checkerboardResolvePipeline.reset();
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
        overlay_.lightGridBuildPipeline.reset();
        overlay_.shadowGpuCullCompactPipeline.reset();
        skin_.pipeline.reset();

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
        frame_.sceneChainCreated = false;
        frame_.sceneChainInitializationPending = false;

        frame_.currentFence = nullptr;
    }

    void VulkanBaseRenderer::RecreateSwapChain()
    {
        // SDL drives the application through callbacks. Never block in the
        // renderer waiting for a restore event: defer the recreation until
        // the next frame after the window becomes visible again.
        if (ctx_.window->IsMinimized())
        {
            requestRecreateSwapChain_ = true;
            return;
        }

        ctx_.device->WaitIdle();
        DeleteSwapChain();
        if (surfaceLost_)
        {
            SPDLOG_INFO("Recreating Vulkan surface after surface loss");
            ctx_.surface->Recreate();
            surfaceLost_ = false;
        }
        CreateSwapChain();
        if (atmosphere_)
        {
            atmosphere_->SyncRuntimeResources(true);
        }
        resetUpscalerHistory_ = true;
        NextEngine::GetInstance()->ResetProgressiveRenderingAccumulation();
    }

    void VulkanBaseRenderer::SetSceneViewportRect(const VkRect2D rect)
    {
        sceneViewport_.requested = rect;
    }

    void VulkanBaseRenderer::ClearSceneViewportRect()
    {
        if (!sceneViewport_.requested.has_value())
        {
            return;
        }
        sceneViewport_.requested.reset();
        if (frame_.swapChain &&
            (sceneViewport_.built.width != frame_.swapChain->Extent().width ||
             sceneViewport_.built.height != frame_.swapChain->Extent().height))
        {
            RequestRecreateSwapChain();
        }
    }

    VkRect2D VulkanBaseRenderer::ResolveSceneViewportRect() const
    {
        const VkExtent2D full = frame_.swapChain->Extent();
        if (!sceneViewport_.requested.has_value() || full.width == 0 || full.height == 0)
        {
            return VkRect2D{{0, 0}, full};
        }

        VkRect2D rect = *sceneViewport_.requested;
        rect.offset.x = std::clamp(rect.offset.x, 0, static_cast<int32_t>(full.width) - 1);
        rect.offset.y = std::clamp(rect.offset.y, 0, static_cast<int32_t>(full.height) - 1);
        rect.extent.width = std::clamp(
            rect.extent.width, 1u, full.width - static_cast<uint32_t>(rect.offset.x));
        rect.extent.height = std::clamp(
            rect.extent.height, 1u, full.height - static_cast<uint32_t>(rect.offset.y));
        return rect;
    }

    void VulkanBaseRenderer::UpdateSceneViewportRect()
    {
        if (!sceneViewport_.requested.has_value() || !frame_.swapChain)
        {
            return;
        }

        const VkRect2D resolved = ResolveSceneViewportRect();
        const bool sizeMatchesResources =
            resolved.extent.width == sceneViewport_.built.width &&
            resolved.extent.height == sceneViewport_.built.height;

        // The render targets, the late tone-mapping images and the upscaler are all sized for
        // sceneViewport_.built. Until a rebuild lands, present no more than that so a viewport that
        // just grew cannot make a pass store past its allocation; a viewport that shrank simply
        // resolves down, exactly as it did before this override existed.
        frame_.swapChain->UpdateOutputViewport(
            resolved.offset.x,
            resolved.offset.y,
            std::min(resolved.extent.width, sceneViewport_.built.width),
            std::min(resolved.extent.height, sceneViewport_.built.height));

        if (sizeMatchesResources)
        {
            sceneViewport_.lastResolved = resolved;
            sceneViewport_.unsettledFrames = 0;
            return;
        }

        const bool settled =
            resolved.extent.width == sceneViewport_.lastResolved.extent.width &&
            resolved.extent.height == sceneViewport_.lastResolved.extent.height;
        sceneViewport_.lastResolved = resolved;
        if (settled || ++sceneViewport_.unsettledFrames >= kSceneViewportResizeDeferFrames)
        {
            RequestRecreateSwapChain();
        }
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
        // Each of these dispatches through a shared pipeline that CreateSceneSwapChainResources
        // built; a renderer without outputs never got them.
        if (!frame_.sceneChainCreated || pipelineWarmup_.active)
        {
            return;
        }

        if (atmosphere_)
        {
            atmosphere_->BeginSceneFrame(commandBuffer, imageIndex);
        }
        rayTracingSceneBackend_->PrepareSceneFrame(commandBuffer, imageIndex);
        ambientCubeBaker_->PrepareSceneFrame(commandBuffer, imageIndex);
        lightGridBuilder_->PrepareSceneFrame(commandBuffer, imageIndex);
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
        if (atmosphere_)
        {
            atmosphere_->PrepareView(commandBuffer, imageIndex, view.IsPrimary(), contract);
        }
        logicRenderer.Render(commandBuffer, imageIndex);
        if (IsReferenceViewAccumulationActive())
        {
            view.State().progressiveFrame = std::min(
                view.State().progressiveFrame + 1,
                frameSettings_.progressiveTargetFrames);
        }
        if (atmosphere_)
        {
            atmosphere_->ApplyToView(commandBuffer, imageIndex, view.IsPrimary(), contract);
        }
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
        // A minimized window has no presentable extent. In particular, do not
        // enter swapchain recreation or vkAcquireNextImageKHR while it is
        // minimized; both paths may wait indefinitely on some WSI drivers.
        if (ctx_.window->IsMinimized())
        {
            return;
        }

        UpdateSceneViewportRect();

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
        frameSettings_.progressiveAccumulatedFrames = engine->GetProgressiveRenderAccumulatedFrames();
        frameSettings_.progressiveTargetFrames = engine->GetProgressiveRenderTargetFrames();
        const auto& settings = frameSettings_.userSettings;
        const bool loading = engine->GetEngineStatus() == NextRenderer::EApplicationStatus::Loading;
        if (atmosphere_)
        {
            atmosphere_->SyncRuntimeResources();
        }
        const bool frameGenerationEnabled = !frameSettings_.progressiveRendering && settings.FrameGeneration &&
            SupportsFrameGeneration(activeUpscalerType_) && temporalSuperResolutionActive_ &&
            !loading;

        // Loading frames only clear the swapchain and render the warm-up overlay. Do not enter
        // the upscaler frame path here: Streamline token/Reflex/marker calls add startup latency
        // while no DLSS or frame-generation work can be recorded yet.
        frame_.streamlineFrameToken = upscaler_ && !loading
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
            needAfterUpdateScene = GetScene().GPUUpdateNodes();
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
#if GK_TRACY_ENABLED
        GkProfiling::BeginGpuFrame(commandBuffer);
#endif

        {
            SCOPED_GPU_TIMER("[gpu]");

            {
                SCOPED_CPU_TIMER("begin scene");
                SCOPED_GPU_TIMER("[pre-render]");
                BeginSceneFrame(commandBuffer, frame_.currentImageIndex);
                GetScene().ClearSkinUpdateRequests();
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
                TransitionSwapchainImage(
                    commandBuffer, frame_.currentImageIndex,
                    {.stages = PipelineCommon::ERenderStage::Transfer,
                     .access = PipelineCommon::EResourceAccess::TransferRead,
                     .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                    "screenshot source");

                VkBufferImageCopy copyRegion{};
                copyRegion.bufferOffset = 0;
                copyRegion.bufferRowLength = 0;
                copyRegion.bufferImageHeight = 0;
                copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copyRegion.imageOffset = {0, 0, 0};
                copyRegion.imageExtent = {SwapChain().Extent().width, SwapChain().Extent().height, 1};

                vkCmdCopyImageToBuffer(commandBuffer, frame_.swapChain->Images()[frame_.currentImageIndex],
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       screenshot_.buffer->Handle(),
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

        const size_t factoryIndex = static_cast<size_t>(type);
        const LogicRendererFactory factory = factoryIndex < RendererFactories.size()
            ? RendererFactories[factoryIndex]
            : nullptr;
        if (factory == nullptr)
        {
            Throw(std::runtime_error(fmt::format(
                "logic renderer implementation '{}' is not installed", GetRendererName(type))));
        }
        auto logicRenderer = factory(*this);
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
        // No compatibility guard: a constrained device registers only ERT_Compatibility, so every
        // other type falls into the unregistered branch below.
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
            GkProfiling::Message(fmt::format("Renderer switched to {}", GetRendererName(type)));
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
            temporalPostFilter_.pingImage == nullptr)
        {
            return false;
        }

        RenderImage& output = *temporalPostFilter_.pingImage;
        const bool initialized = temporalPostFilter_.pingInitialized;
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
        temporalPostFilter_.pingInitialized = true;
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
        if (temporalPostFilter_.pingImage == nullptr ||
            temporalPostFilter_.pongImage == nullptr ||
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
            // Jitter of the frame that produced the normal/albedo guides, so the guide lookup can
            // be realigned onto the resolved display image.
            float jitterX;
            float jitterY;
            float compressionScale;
            uint32_t useAlbedoGuide;
        };
        static_assert(sizeof(FPushConstants) == 64);

        const float guideJitterX = inputs.ubo != nullptr ? inputs.ubo->Jitter.x : 0.0f;
        const float guideJitterY = inputs.ubo != nullptr ? inputs.ubo->Jitter.y : 0.0f;
        const float compressionScale = inputs.ubo != nullptr
            ? Rendering::Upscaler::ComputeHdrCompressionScale(
                  inputs.ubo->PaperWhiteNit, inputs.ubo->HDR)
            : 1.0f;
        // Not every renderer contract writes RT_ALBEDO. SoftwareModernNoAmbient declares only
        // Color/Depth/Motion/ObjectId/Normal, so guiding on that image would weight the kernel by
        // whatever another renderer last left there.
        const bool useAlbedoGuide = HasAny(
            GetRendererContract(logicRenderers_.current).outputs, ERenderOutput::Albedo);

        RenderImage* sourceImage = temporalPostFilter_.pingImage.get();
        uint32_t sourceIndex = Assets::Bindless::RT_TEMPORAL_POST_PING;
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
            uint32_t destinationIndex = 0;
            int32_t destinationOffsetX = 0;
            int32_t destinationOffsetY = 0;
            if (finalPass)
            {
                destinationImage = lateToneMapping_.inputImage.get();
                destinationIndex = Assets::Bindless::RT_TONEMAP_INPUT;
                const bool initialized = lateToneMapping_.inputInitialized;
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
                lateToneMapping_.inputInitialized = true;
            }
            else
            {
                const bool sourceIsPing = sourceIndex == Assets::Bindless::RT_TEMPORAL_POST_PING;
                destinationImage = sourceIsPing
                    ? temporalPostFilter_.pongImage.get()
                    : temporalPostFilter_.pingImage.get();
                destinationIndex = (sourceIsPing
                    ? Assets::Bindless::RT_TEMPORAL_POST_PONG
                    : Assets::Bindless::RT_TEMPORAL_POST_PING);
                const bool destinationInitialized = sourceIsPing
                    ? temporalPostFilter_.pongInitialized
                    : temporalPostFilter_.pingInitialized;
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
                    temporalPostFilter_.pongInitialized = true;
                }
                else
                {
                    temporalPostFilter_.pingInitialized = true;
                }
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
                guideJitterX,
                guideJitterY,
                compressionScale,
                useAlbedoGuide ? 1u : 0u,
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
            lateToneMapping_.inputImage == nullptr || lateToneMapping_.outputImage == nullptr)
        {
            return;
        }

        SCOPED_GPU_TIMER("tone mapping after upscaler");
        constexpr VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        uint32_t inputIndex = Assets::Bindless::RT_SCENE_COLOR;
        VkExtent2D inputExtent = sourceExtent;
        if (sourceIsUpscaled)
        {
            inputIndex = Assets::Bindless::RT_TONEMAP_INPUT;
            inputExtent = outputExtent;
            RenderImage* inputImage = lateToneMapping_.inputImage.get();
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

        constexpr uint32_t outputIndex = Assets::Bindless::RT_TONEMAP_OUTPUT;
        RenderImage* offscreenOutput = lateToneMapping_.outputImage.get();
        const bool initialized = lateToneMapping_.outputInitialized;
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
        lateToneMapping_.outputInitialized = true;

        TransitionSwapchainImage(
            commandBuffer, imageIndex,
            {.stages = PipelineCommon::ERenderStage::Transfer,
             .access = PipelineCommon::EResourceAccess::TransferWrite,
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
            auto inputs = BuildUpscalerFrameInputs(commandBuffer, imageIndex, VK_IMAGE_LAYOUT_GENERAL);
            const auto& typeInfo = Rendering::Upscaler::GetUpscalerTypeInfo(
                static_cast<uint32_t>(activeUpscalerType_));
            inputs.inputColorIsLinear = true;
            RenderImage* toneMappingInput = lateToneMapping_.inputImage.get();
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
                const bool initialized = lateToneMapping_.inputInitialized;
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
                lateToneMapping_.inputInitialized = true;
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
                temporalPostFilter_.pingInitialized = false;
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

    void VulkanBaseRenderer::ClearWarmupSwapchain(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        // A newly acquired swapchain image has undefined contents. Keep every frame of the
        // pipeline warm-up deterministic before the UI pass loads it.
        const VkClearColorValue clearColor = {{0.05f, 0.05f, 0.06f, 1.0f}};
        const VkImageSubresourceRange imageRange{
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        TransitionSwapchainImage(
            commandBuffer, imageIndex,
            {.stages = PipelineCommon::ERenderStage::Transfer,
             .access = PipelineCommon::EResourceAccess::TransferWrite,
             .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
             .discardPreviousContents = true},
            "pipeline warm-up clear");
        vkCmdClearColorImage(commandBuffer, SwapChain().Images()[imageIndex],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &imageRange);
        TransitionSwapchainImage(
            commandBuffer, imageIndex,
            {.stages = PipelineCommon::ERenderStage::Present,
             .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
            "pipeline warm-up present");
    }

    void VulkanBaseRenderer::Render(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        // The loading overlay owns the whole presentation surface until warm-up completes. This
        // also keeps later warm-up frames from entering the normal scene-output path.
        if (pipelineWarmup_.active)
        {
            ClearWarmupSwapchain(commandBuffer, imageIndex);
            return;
        }

        // A renderer with no declared outputs owns the swapchain image: there is no RT bank to
        // resolve from, and none of the view/overlay machinery below has resources. It writes the
        // image itself, then hands it to the UI pass in PRESENT_SRC like every other path.
        if (!frame_.sceneChainCreated)
        {
            if (LogicRendererBase* renderer = EnsureLogicRenderer(logicRenderers_.current))
            {
                renderer->Render(commandBuffer, imageIndex);
            }
            TransitionSwapchainImage(
                commandBuffer, imageIndex,
                {.stages = PipelineCommon::ERenderStage::Present,
                 .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
                "compatibility present");
            return;
        }

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

            if (!overlay_.skipReportedRendererValid ||
                overlay_.skipReportedRendererType != logicRenderers_.current)
            {
                overlay_.skipReportedPassNames.clear();
                overlay_.skipReportedRendererType = logicRenderers_.current;
                overlay_.skipReportedRendererValid = true;
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
                    if (std::find(overlay_.skipReportedPassNames.begin(), overlay_.skipReportedPassNames.end(),
                                  passContract.name) == overlay_.skipReportedPassNames.end())
                    {
                        overlay_.skipReportedPassNames.push_back(passContract.name);
                        SPDLOG_INFO("Skipping external pass {} under {}: incompatible insertion/scope or missing "
                                    "outputs (required=0x{:x}, available=0x{:x})",
                                    passContract.name, GetRendererName(logicRenderers_.current),
                                    passContract.requiredOutputs, availableOutputs);
                    }
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
        if (frame_.sceneChainInitializationPending || pipelineWarmup_.active)
        {
            return;
        }

        if (ActiveRendererRequirements().requestAmbientCube && !ShouldSkipAmbientCubeUpdates())
        {
            const bool useHardware = caps_.supportRayTracing && !GOption->ForceSoftGen;
            BakeAmbientCubeCascade(commandBuffer, imageIndex, useHardware);
        }

        DispatchVisualDebugger(commandBuffer, imageIndex);
    }

    void VulkanBaseRenderer::UpdateUniformBuffer(const uint32_t imageIndex)
    {
        if (frame_.sceneChainInitializationPending || pipelineWarmup_.active)
        {
            return;
        }

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
            frame_.swapChain->Extent().width,
            frame_.swapChain->Extent().height,
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

        const Assets::Camera camera = NextEngine::GetInstance()->GetActiveRenderCamera();
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
            // Keep non-shadowing geometry available to primary and area-light rays, but keep it
            // out of the dedicated sun-occlusion mask used by the hardware path tracer.
            constexpr uint8_t rayMaskAll = 0xFF;
            constexpr uint8_t rayMaskNonShadowCaster = 0x02;
            const uint8_t rayMask = includeInGpuAs
                ? ((node.visible & Runtime::RenderParticipation::shadowCaster) != 0u
                       ? rayMaskAll
                       : rayMaskNonShadowCaster)
                : 0u;
            instances.push_back(RayTracing::TopLevelAccelerationStructure::CreateInstance(
                rt_->blas[blasIndex], glm::transpose(node.worldTS), node.instanceId, rayMask));
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
