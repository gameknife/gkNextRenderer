#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextFidelityFX/FidelityFXIntegration.hpp"

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/Upscaler/IUpscaler.hpp"
#include "Engine/Rendering/Upscaler/UpscalerTypes.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/DeviceCreationAugmenter.hpp"
#include "Engine/Vulkan/VulkanInterposer.hpp"

#if WITH_FIDELITYFX && WIN32
#include <ffx_api/ffx_api.hpp>
#include <ffx_api/ffx_framegeneration.hpp>
#include <ffx_api/ffx_upscale.hpp>
#include <ffx_api/vk/ffx_api_vk.hpp>

#include <array>
#include <cfloat>
#include <map>
#include <mutex>
#include <optional>
#include <tuple>
#endif

namespace FidelityFXWrapper
{
#if WITH_FIDELITYFX && WIN32
    namespace
    {
        struct FQueueSlot
        {
            uint32_t family = UINT32_MAX;
            uint32_t index = UINT32_MAX;
            VkQueueFlags flags = 0;
            bool supportsPresent = false;

            bool IsValid() const { return family != UINT32_MAX && index != UINT32_MAX; }
            auto Key() const { return std::pair(family, index); }
        };

        struct FQueuePlan
        {
            FQueueSlot game;
            FQueueSlot asyncCompute;
            FQueueSlot present;
            FQueueSlot imageAcquire;

            bool IsValid() const
            {
                return game.IsValid() && asyncCompute.IsValid() && present.IsValid() && imageAcquire.IsValid();
            }
        };

        uint32_t ToFfxQualityMode(uint32_t rawMode)
        {
            using Rendering::Upscaler::EUpscaleMode;
            switch (Rendering::Upscaler::GetUpscaleModeInfo(rawMode).mode)
            {
            case EUpscaleMode::Native: return FFX_UPSCALE_QUALITY_MODE_NATIVEAA;
            case EUpscaleMode::Balanced: return FFX_UPSCALE_QUALITY_MODE_BALANCED;
            case EUpscaleMode::Performance: return FFX_UPSCALE_QUALITY_MODE_PERFORMANCE;
            case EUpscaleMode::UltraPerformance: return FFX_UPSCALE_QUALITY_MODE_ULTRA_PERFORMANCE;
            case EUpscaleMode::Quality:
            case EUpscaleMode::Auto:
            default: return FFX_UPSCALE_QUALITY_MODE_QUALITY;
            }
        }

        FfxApiResource ToFfxResource(const Rendering::Upscaler::FImageResource& image, uint32_t state)
        {
            if (!image.IsValid())
            {
                return {};
            }

            VkImageCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            createInfo.imageType = VK_IMAGE_TYPE_2D;
            createInfo.format = image.format;
            createInfo.extent = {image.extent.width, image.extent.height, 1};
            createInfo.mipLevels = 1;
            createInfo.arrayLayers = 1;
            createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            createInfo.usage = image.usage;
            createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            const FfxApiResourceDescription description =
                ffxApiGetImageResourceDescriptionVK(image.image, createInfo, 0);
            return ffxApiGetResourceVK(reinterpret_cast<void*>(image.image), description, state);
        }

        bool FeatureChainContains(const void* featureChain, VkStructureType sType)
        {
            for (const auto* node = static_cast<const VkBaseInStructure*>(featureChain);
                 node != nullptr; node = node->pNext)
            {
                if (node->sType == sType)
                {
                    return true;
                }
            }
            return false;
        }

        class FFidelityFXContext final
        {
        public:
            void PlanQueues(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                             std::map<uint32_t, uint32_t>& queueCounts)
            {
                std::lock_guard lock(mutex_);
                queuePlan_ = {};
                const std::map<uint32_t, uint32_t> engineQueueCounts = queueCounts;

                uint32_t familyCount = 0;
                vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
                std::vector<VkQueueFamilyProperties> properties(familyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, properties.data());

                std::vector<FQueueSlot> slots;
                for (uint32_t family = 0; family < familyCount; ++family)
                {
                    VkBool32 supportsPresent = VK_FALSE;
                    vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, family, surface, &supportsPresent);
                    for (uint32_t index = 0; index < properties[family].queueCount; ++index)
                    {
                        slots.push_back({family, index, properties[family].queueFlags, supportsPresent == VK_TRUE});
                    }
                }

                const auto graphics = std::find_if(slots.begin(), slots.end(), [](const FQueueSlot& slot)
                {
                    return slot.index == 0 && (slot.flags & VK_QUEUE_GRAPHICS_BIT) != 0;
                });
                const auto game = std::find_if(slots.begin(), slots.end(), [](const FQueueSlot& slot)
                {
                    return slot.index == 0 && slot.supportsPresent &&
                           (slot.flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                               (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
                });
                if (graphics == slots.end() || game == slots.end() || graphics->Key() != game->Key())
                {
                    SPDLOG_WARN("FidelityFX Frame Generation disabled: the engine graphics/present queue is not SDK-compatible");
                    return;
                }

                const auto isReservedByEngine = [&engineQueueCounts](const FQueueSlot& slot)
                {
                    const auto family = engineQueueCounts.find(slot.family);
                    return family != engineQueueCounts.end() && slot.index < family->second;
                };

                std::optional<std::tuple<int, FQueueSlot, FQueueSlot, FQueueSlot>> best;
                for (const FQueueSlot& async : slots)
                {
                    if (async.Key() == game->Key() || isReservedByEngine(async) ||
                        (async.flags & VK_QUEUE_COMPUTE_BIT) == 0)
                    {
                        continue;
                    }
                    for (const FQueueSlot& present : slots)
                    {
                        if (present.Key() == game->Key() || present.Key() == async.Key() ||
                            isReservedByEngine(present) ||
                            !present.supportsPresent ||
                            (present.flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT)) == 0)
                        {
                            continue;
                        }
                        for (const FQueueSlot& acquire : slots)
                        {
                            if (acquire.Key() == game->Key() || acquire.Key() == async.Key() ||
                                acquire.Key() == present.Key() || isReservedByEngine(acquire))
                            {
                                continue;
                            }

                            int score = 0;
                            score += async.family != game->family ? 8 : 0;
                            score += (async.flags & VK_QUEUE_GRAPHICS_BIT) == 0 ? 4 : 0;
                            score += present.family == game->family ? 3 : 0;
                            score += (acquire.flags & VK_QUEUE_GRAPHICS_BIT) == 0 ? 2 : 0;
                            score -= static_cast<int>(async.index + present.index + acquire.index);
                            if (!best || score > std::get<0>(*best))
                            {
                                best = std::tuple(score, async, present, acquire);
                            }
                        }
                    }
                }

                if (!best)
                {
                    SPDLOG_WARN("FidelityFX Frame Generation disabled: the Vulkan device cannot expose four distinct queues");
                    return;
                }

                queuePlan_.game = *game;
                queuePlan_.asyncCompute = std::get<1>(*best);
                queuePlan_.present = std::get<2>(*best);
                queuePlan_.imageAcquire = std::get<3>(*best);
                for (const FQueueSlot& slot : {queuePlan_.game, queuePlan_.asyncCompute,
                                               queuePlan_.present, queuePlan_.imageAcquire})
                {
                    queueCounts[slot.family] = std::max(queueCounts[slot.family], slot.index + 1);
                }

                SPDLOG_INFO("FidelityFX FG queues: game={}:{}, async={}:{}, present={}:{}, acquire={}:{}",
                            queuePlan_.game.family, queuePlan_.game.index,
                            queuePlan_.asyncCompute.family, queuePlan_.asyncCompute.index,
                            queuePlan_.present.family, queuePlan_.present.index,
                            queuePlan_.imageAcquire.family, queuePlan_.imageAcquire.index);
            }

            void SetDevice(const Rendering::Upscaler::FDeviceInfo& info)
            {
                std::lock_guard lock(mutex_);
                deviceInfo_ = info;
                deviceReady_ = info.device != VK_NULL_HANDLE && info.physicalDevice != VK_NULL_HANDLE;
                if (!deviceReady_ || !queuePlan_.IsValid())
                {
                    return;
                }

                GetQueue(queuePlan_.game, gameQueue_);
                GetQueue(queuePlan_.asyncCompute, asyncComputeQueue_);
                GetQueue(queuePlan_.present, presentQueue_);
                GetQueue(queuePlan_.imageAcquire, imageAcquireQueue_);
                frameGenerationAvailable_ = gameQueue_ != VK_NULL_HANDLE && asyncComputeQueue_ != VK_NULL_HANDLE &&
                                            presentQueue_ != VK_NULL_HANDLE && imageAcquireQueue_ != VK_NULL_HANDLE;
            }

            bool IsDeviceReady() const { return deviceReady_; }
            bool IsFrameGenerationAvailable() const { return frameGenerationAvailable_; }

            bool CreateSwapchain(const VkSwapchainCreateInfoKHR* createInfo,
                                  const VkAllocationCallbacks* allocator,
                                  VkSwapchainKHR* swapchain)
            {
                NextEngine* engine = NextEngine::GetInstance();
                const bool frameGenerationRequested = engine != nullptr &&
                    engine->GetUserSettings().FSR && engine->GetUserSettings().FSRG;
                SPDLOG_INFO("FidelityFX swapchain path: proxyRequested={}, proxyAvailable={}",
                            frameGenerationRequested, frameGenerationAvailable_);
                if (!frameGenerationAvailable_ || !frameGenerationRequested)
                {
                    activeProxySwapchain_ = VK_NULL_HANDLE;
                    return false;
                }

                if (swapchainContext_ == nullptr)
                {
                    ffx::CreateContextDescFrameGenerationSwapChainVK createSwapchain{};
                    createSwapchain.physicalDevice = deviceInfo_.physicalDevice;
                    createSwapchain.device = deviceInfo_.device;
                    createSwapchain.swapchain = swapchain;
                    createSwapchain.allocator = const_cast<VkAllocationCallbacks*>(allocator);
                    createSwapchain.createInfo = *createInfo;
                    FillQueueInfo(createSwapchain.gameQueue, queuePlan_.game, gameQueue_);
                    FillQueueInfo(createSwapchain.asyncComputeQueue, queuePlan_.asyncCompute, asyncComputeQueue_);
                    FillQueueInfo(createSwapchain.presentQueue, queuePlan_.present, presentQueue_);
                    FillQueueInfo(createSwapchain.imageAcquireQueue, queuePlan_.imageAcquire, imageAcquireQueue_);

                    const ffx::ReturnCode result = ffx::CreateContext(swapchainContext_, nullptr, createSwapchain);
                    if (result != ffx::ReturnCode::Ok)
                    {
                        SPDLOG_ERROR("FidelityFX frame-generation swapchain creation failed: {}",
                                     static_cast<uint32_t>(result));
                        swapchainContext_ = nullptr;
                        frameGenerationAvailable_ = false;
                        return false;
                    }

                    ffx::QueryDescSwapchainReplacementFunctionsVK functions{};
                    const ffx::ReturnCode queryResult = ffx::Query(swapchainContext_, functions);
                    if (queryResult != ffx::ReturnCode::Ok)
                    {
                        SPDLOG_ERROR("FidelityFX swapchain function query failed: {}",
                                     static_cast<uint32_t>(queryResult));
                        ffx::DestroyContext(swapchainContext_);
                        swapchainContext_ = nullptr;
                        frameGenerationAvailable_ = false;
                        return false;
                    }
                    replacementFunctions_ = functions;
                    activeProxySwapchain_ = *swapchain;
                    return true;
                }

                if (replacementFunctions_.pOutCreateSwapchainFFXAPI == nullptr)
                {
                    return false;
                }
                const VkResult result = replacementFunctions_.pOutCreateSwapchainFFXAPI(
                    deviceInfo_.device, createInfo, allocator, swapchain, swapchainContext_);
                if (result == VK_SUCCESS)
                {
                    activeProxySwapchain_ = *swapchain;
                    return true;
                }
                return false;
            }

            void DestroySwapchain(VkDevice device, VkSwapchainKHR swapchain,
                                  const VkAllocationCallbacks* allocator)
            {
                if (swapchain == activeProxySwapchain_ && swapchainContext_ != nullptr &&
                    replacementFunctions_.pOutDestroySwapchainFFXAPI != nullptr)
                {
                    replacementFunctions_.pOutDestroySwapchainFFXAPI(
                        device, swapchain, allocator, swapchainContext_);
                    activeProxySwapchain_ = VK_NULL_HANDLE;
                    loggedProxyPresent_ = false;
                    return;
                }
                vkDestroySwapchainKHR(device, swapchain, allocator);
            }

            VkResult GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain,
                                        uint32_t* count, VkImage* images) const
            {
                const bool proxy = swapchain == activeProxySwapchain_ &&
                    replacementFunctions_.pOutGetSwapchainImagesKHR != nullptr;
                return proxy
                    ? replacementFunctions_.pOutGetSwapchainImagesKHR(device, swapchain, count, images)
                    : vkGetSwapchainImagesKHR(device, swapchain, count, images);
            }

            VkResult AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                      VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex) const
            {
                return swapchain == activeProxySwapchain_ &&
                           replacementFunctions_.pOutAcquireNextImageKHR != nullptr
                    ? replacementFunctions_.pOutAcquireNextImageKHR(
                          device, swapchain, timeout, semaphore, fence, imageIndex)
                    : vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, imageIndex);
            }

            VkResult QueuePresent(VkQueue queue, const VkPresentInfoKHR* presentInfo)
            {
                const bool proxyPresent = presentInfo != nullptr && presentInfo->swapchainCount == 1 &&
                    presentInfo->pSwapchains != nullptr && presentInfo->pSwapchains[0] == activeProxySwapchain_;
                const VkResult result = proxyPresent && replacementFunctions_.pOutQueuePresentKHR != nullptr
                    ? replacementFunctions_.pOutQueuePresentKHR(queue, presentInfo)
                    : vkQueuePresentKHR(queue, presentInfo);
                if (proxyPresent && result == VK_SUCCESS && !loggedProxyPresent_)
                {
                    SPDLOG_INFO("FidelityFX proxy present is active");
                    loggedProxyPresent_ = true;
                }
                return result;
            }

            void WaitForPresents()
            {
                if (swapchainContext_ != nullptr)
                {
                    ffx::DispatchDescFrameGenerationSwapChainWaitForPresentsVK wait{};
                    const ffx::ReturnCode result = ffx::Dispatch(swapchainContext_, wait);
                    if (result != ffx::ReturnCode::Ok)
                    {
                        SPDLOG_WARN("FidelityFX wait-for-presents failed: {}", static_cast<uint32_t>(result));
                    }
                }
            }

            bool EnsureUpscaleContext(VkExtent2D renderExtent, VkExtent2D outputExtent, bool hdrOutput)
            {
                if (!deviceReady_ || renderExtent.width == 0 || renderExtent.height == 0 ||
                    outputExtent.width == 0 || outputExtent.height == 0)
                {
                    return false;
                }
                if (upscaleContext_ != nullptr && renderExtent.width <= upscaleMaxRenderExtent_.width &&
                    renderExtent.height <= upscaleMaxRenderExtent_.height && outputExtent.width == upscaleOutputExtent_.width &&
                    outputExtent.height == upscaleOutputExtent_.height && hdrOutput == upscaleHdrOutput_)
                {
                    return true;
                }

                DestroyUpscaleContext();
                ffx::CreateContextDescUpscale create{};
                create.maxRenderSize = {renderExtent.width, renderExtent.height};
                create.maxUpscaleSize = {outputExtent.width, outputExtent.height};
                create.flags = FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
                if (hdrOutput)
                {
                    create.flags |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
                }
                create.fpMessage = nullptr;

                ffx::CreateBackendVKDesc backend{};
                backend.vkDevice = deviceInfo_.device;
                backend.vkPhysicalDevice = deviceInfo_.physicalDevice;
                backend.vkDeviceProcAddr = vkGetDeviceProcAddr;
                SPDLOG_INFO("Creating FidelityFX upscale context: {}x{} -> {}x{}, HDR={}",
                            renderExtent.width, renderExtent.height,
                            outputExtent.width, outputExtent.height, hdrOutput);
                const ffx::ReturnCode result = ffx::CreateContext(upscaleContext_, nullptr, create, backend);
                if (result != ffx::ReturnCode::Ok)
                {
                    SPDLOG_ERROR("FidelityFX FSR upscale context creation failed: {}",
                                 static_cast<uint32_t>(result));
                    upscaleContext_ = nullptr;
                    return false;
                }

                upscaleMaxRenderExtent_ = renderExtent;
                upscaleOutputExtent_ = outputExtent;
                upscaleHdrOutput_ = hdrOutput;
                return true;
            }

            bool EnsureFrameGenerationContext(const Rendering::Upscaler::FFrameInputs& inputs)
            {
                if (!frameGenerationAvailable_ || swapchainContext_ == nullptr)
                {
                    return false;
                }
                if (frameGenerationContext_ != nullptr && frameGenerationExtent_.width == inputs.outputExtent.width &&
                    frameGenerationExtent_.height == inputs.outputExtent.height &&
                    frameGenerationFormat_ == inputs.swapchainFormat &&
                    frameGenerationHdrOutput_ == inputs.hdrOutput)
                {
                    return true;
                }

                DestroyFrameGenerationContext();
                ffx::CreateContextDescFrameGeneration create{};
                create.flags = FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT;
                if (inputs.hdrOutput)
                {
                    create.flags |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
                }
                create.displaySize = {inputs.outputExtent.width, inputs.outputExtent.height};
                create.maxRenderSize = {inputs.outputExtent.width, inputs.outputExtent.height};
                create.backBufferFormat = ffxApiGetSurfaceFormatVK(inputs.swapchainFormat);

                ffx::CreateBackendVKDesc backend{};
                backend.vkDevice = deviceInfo_.device;
                backend.vkPhysicalDevice = deviceInfo_.physicalDevice;
                backend.vkDeviceProcAddr = vkGetDeviceProcAddr;

                ffx::CreateContextDescFrameGenerationHudless hudless{};
                hudless.hudlessBackBufferFormat = ffxApiGetSurfaceFormatVK(inputs.hudlessColor.format);
                // FidelityFX SDK 1.1.4's variadic C++ helper cannot link three descriptors
                // with MSVC because its recursive overload is declared later in the header.
                // Build the documented pNext chain explicitly and call the C entry point.
                create.header.pNext = &backend.header;
                backend.header.pNext = &hudless.header;
                hudless.header.pNext = nullptr;
                const ffx::ReturnCode result = static_cast<ffx::ReturnCode>(
                    ffxCreateContext(&frameGenerationContext_, &create.header, nullptr));
                if (result != ffx::ReturnCode::Ok)
                {
                    SPDLOG_ERROR("FidelityFX Frame Generation context creation failed: {}",
                                 static_cast<uint32_t>(result));
                    frameGenerationContext_ = nullptr;
                    return false;
                }

                frameGenerationExtent_ = inputs.outputExtent;
                frameGenerationFormat_ = inputs.swapchainFormat;
                frameGenerationHdrOutput_ = inputs.hdrOutput;
                return true;
            }

            ffx::Context& UpscaleContext() { return upscaleContext_; }
            ffx::Context& FrameGenerationContext() { return frameGenerationContext_; }

            uint32_t QueryJitterPhaseCount(VkExtent2D renderExtent, VkExtent2D outputExtent, bool hdrOutput)
            {
                if (!EnsureUpscaleContext(renderExtent, outputExtent, hdrOutput))
                {
                    return 0;
                }
                int32_t phaseCount = 0;
                ffx::QueryDescUpscaleGetJitterPhaseCount query{};
                query.renderWidth = renderExtent.width;
                query.displayWidth = outputExtent.width;
                query.pOutPhaseCount = &phaseCount;
                if (ffx::Query(upscaleContext_, query) != ffx::ReturnCode::Ok)
                {
                    return 0;
                }
                return static_cast<uint32_t>(std::max(phaseCount, 0));
            }

            void DisableFrameGeneration(VkSwapchainKHR swapchain)
            {
                if (frameGenerationContext_ == nullptr)
                {
                    return;
                }
                ffx::ConfigureDescFrameGeneration config{};
                config.swapChain = reinterpret_cast<void*>(swapchain);
                config.frameGenerationEnabled = false;
                config.frameID = lastFrameId_;
                ffx::Configure(frameGenerationContext_, config);
            }

            uint64_t LastPresentCount(VkSwapchainKHR swapchain) const
            {
                return replacementFunctions_.pOutGetLastPresentCountFFXAPI != nullptr
                    ? replacementFunctions_.pOutGetLastPresentCountFFXAPI(swapchain)
                    : 0;
            }

            void SetLastFrameId(uint64_t frameId) { lastFrameId_ = frameId; }

            void DestroyEffectContexts(VkSwapchainKHR swapchain)
            {
                DisableFrameGeneration(swapchain);
                DestroyFrameGenerationContext();
                DestroyUpscaleContext();
            }

            void Shutdown()
            {
                DestroyEffectContexts(VK_NULL_HANDLE);
                WaitForPresents();
                if (swapchainContext_ != nullptr)
                {
                    ffx::DestroyContext(swapchainContext_);
                    swapchainContext_ = nullptr;
                }
                replacementFunctions_ = {};
                activeProxySwapchain_ = VK_NULL_HANDLE;
                loggedProxyPresent_ = false;
                deviceReady_ = false;
                frameGenerationAvailable_ = false;
            }

        private:
            void GetQueue(const FQueueSlot& slot, VkQueue& queue) const
            {
                vkGetDeviceQueue(deviceInfo_.device, slot.family, slot.index, &queue);
            }

            static void FillQueueInfo(VkQueueInfoFFXAPI& info, const FQueueSlot& slot, VkQueue queue)
            {
                info.queue = queue;
                info.familyIndex = slot.family;
                info.submitFunc = nullptr;
            }

            void DestroyUpscaleContext()
            {
                if (upscaleContext_ != nullptr)
                {
                    ffx::DestroyContext(upscaleContext_);
                    upscaleContext_ = nullptr;
                }
                jitterPhaseCount_ = 0;
                upscaleHdrOutput_ = false;
            }

            void DestroyFrameGenerationContext()
            {
                if (frameGenerationContext_ != nullptr)
                {
                    ffx::DestroyContext(frameGenerationContext_);
                    frameGenerationContext_ = nullptr;
                }
                frameGenerationHdrOutput_ = false;
            }

            mutable std::mutex mutex_;
            Rendering::Upscaler::FDeviceInfo deviceInfo_{};
            FQueuePlan queuePlan_{};
            VkQueue gameQueue_ = VK_NULL_HANDLE;
            VkQueue asyncComputeQueue_ = VK_NULL_HANDLE;
            VkQueue presentQueue_ = VK_NULL_HANDLE;
            VkQueue imageAcquireQueue_ = VK_NULL_HANDLE;
            bool deviceReady_ = false;
            bool frameGenerationAvailable_ = false;

            ffx::Context swapchainContext_ = nullptr;
            ffx::Context upscaleContext_ = nullptr;
            ffx::Context frameGenerationContext_ = nullptr;
            ffxQueryDescSwapchainReplacementFunctionsVK replacementFunctions_{};
            VkSwapchainKHR activeProxySwapchain_ = VK_NULL_HANDLE;
            bool loggedProxyPresent_ = false;
            VkExtent2D upscaleMaxRenderExtent_{};
            VkExtent2D upscaleOutputExtent_{};
            bool upscaleHdrOutput_ = false;
            VkExtent2D frameGenerationExtent_{};
            VkFormat frameGenerationFormat_ = VK_FORMAT_UNDEFINED;
            bool frameGenerationHdrOutput_ = false;
            uint32_t jitterPhaseCount_ = 0;
            uint64_t lastFrameId_ = 0;
        };

        FFidelityFXContext& Context()
        {
            static FFidelityFXContext context;
            return context;
        }

        class FFidelityFXDeviceAugmenter final : public Vulkan::IDeviceCreationAugmenter
        {
        public:
            void* OnPhysicalDeviceSelected(VkInstance, VkPhysicalDevice physicalDevice,
                                            std::vector<const char*>& requiredExtensions,
                                            void* featureChain) override
            {
                const auto availableExtensions = Vulkan::GetEnumerateVector(
                    physicalDevice, static_cast<const char*>(nullptr),
                    vkEnumerateDeviceExtensionProperties);
                const auto addExtensionIfAvailable = [&](const char* extensionName)
                {
                    const bool available = std::any_of(
                        availableExtensions.begin(), availableExtensions.end(),
                        [extensionName](const VkExtensionProperties& extension)
                        {
                            return std::strcmp(extension.extensionName, extensionName) == 0;
                        });
                    if (available && std::find(requiredExtensions.begin(), requiredExtensions.end(),
                                               extensionName) == requiredExtensions.end())
                    {
                        requiredExtensions.push_back(extensionName);
                    }
                    return available;
                };

                // SDK 1.1.4 checks the physical-device extension list and then calls the
                // KHR-suffixed memory-requirements entry point. Enable both promoted
                // extensions so vkGetDeviceProcAddr cannot return null on Vulkan 1.1+.
                addExtensionIfAvailable(VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
                addExtensionIfAvailable(VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME);

                if (!FeatureChainContains(
                        featureChain, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES))
                {
                    timelineSemaphoreFeatures_ = {};
                    timelineSemaphoreFeatures_.sType =
                        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
                    timelineSemaphoreFeatures_.timelineSemaphore = VK_TRUE;
                    timelineSemaphoreFeatures_.pNext = featureChain;
                    return &timelineSemaphoreFeatures_;
                }
                return featureChain;
            }

            void AugmentQueueRequests(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
                                      std::map<uint32_t, uint32_t>& queueCounts) override
            {
                Context().PlanQueues(physicalDevice, surface, queueCounts);
            }

        private:
            VkPhysicalDeviceTimelineSemaphoreFeatures timelineSemaphoreFeatures_{};
        };

        class FFidelityFXInterposer final : public Vulkan::IVulkanInterposer
        {
        public:
            VkResult DeviceWaitIdle(VkDevice device) override
            {
                Context().WaitForPresents();
                return vkDeviceWaitIdle(device);
            }

            VkResult CreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* createInfo,
                                        const VkAllocationCallbacks* allocator,
                                        VkSwapchainKHR* swapchain) override
            {
                if (Context().CreateSwapchain(createInfo, allocator, swapchain))
                {
                    return VK_SUCCESS;
                }
                return vkCreateSwapchainKHR(device, createInfo, allocator, swapchain);
            }

            void DestroySwapchainKHR(VkDevice device, VkSwapchainKHR swapchain,
                                     const VkAllocationCallbacks* allocator) override
            {
                Context().DestroySwapchain(device, swapchain, allocator);
            }

            VkResult GetSwapchainImagesKHR(VkDevice device, VkSwapchainKHR swapchain,
                                           uint32_t* count, VkImage* images) override
            {
                return Context().GetSwapchainImages(device, swapchain, count, images);
            }

            VkResult AcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                         VkSemaphore semaphore, VkFence fence, uint32_t* imageIndex) override
            {
                return Context().AcquireNextImage(device, swapchain, timeout, semaphore, fence, imageIndex);
            }

            VkResult QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* presentInfo) override
            {
                return Context().QueuePresent(queue, presentInfo);
            }
        };

        class FFidelityFXUpscaler final : public Rendering::Upscaler::IUpscaler
        {
        public:
            void OnDeviceCreated(const Rendering::Upscaler::FDeviceInfo& deviceInfo,
                                 Rendering::Upscaler::FFeatureCaps& caps) override
            {
                Context().SetDevice(deviceInfo);
                caps.provider = Rendering::Upscaler::EUpscalerProvider::FidelityFX;
                caps.supportFSR = Context().IsDeviceReady();
                caps.supportFSRFrameGeneration = Context().IsFrameGenerationAvailable();
                SPDLOG_INFO("FidelityFX Vulkan device ready. FSR={}, Frame Generation={}",
                            caps.supportFSR, caps.supportFSRFrameGeneration);
            }

            void OnSwapChainDestroyed() override
            {
                Context().DestroyEffectContexts(lastSwapchain_);
                lastSwapchain_ = VK_NULL_HANDLE;
                jitterPhaseCount_ = 0;
                frameGenerationState_ = {};
                loggedUpscaleDispatch_ = false;
                loggedFrameGenerationDispatch_ = false;
            }

            void Shutdown() override
            {
                Context().Shutdown();
            }

            Rendering::Upscaler::FOptimalRenderSettings GetOptimalRenderSettings(
                uint32_t superResolutionMode, VkExtent2D outputExtent, bool upscalerEnabled,
                bool hdrOutput, Rendering::Upscaler::EUpscalerProvider) override
            {
                Rendering::Upscaler::FOptimalRenderSettings result{};
                const auto& modeInfo = Rendering::Upscaler::GetUpscaleModeInfo(superResolutionMode);
                result.renderExtent = Rendering::Upscaler::ScaleExtent(outputExtent, modeInfo.fallbackScale);
                result.minRenderExtent = result.renderExtent;
                result.maxRenderExtent = result.renderExtent;
                if (!upscalerEnabled)
                {
                    return result;
                }

                ffx::QueryDescUpscaleGetRenderResolutionFromQualityMode query{};
                query.displayWidth = outputExtent.width;
                query.displayHeight = outputExtent.height;
                query.qualityMode = ToFfxQualityMode(superResolutionMode);
                query.pOutRenderWidth = &result.renderExtent.width;
                query.pOutRenderHeight = &result.renderExtent.height;
                if (ffx::Query(query) != ffx::ReturnCode::Ok)
                {
                    SPDLOG_WARN("FidelityFX render-resolution query failed; using fallback scale {}",
                                modeInfo.fallbackScale);
                }
                result.minRenderExtent = result.renderExtent;
                result.maxRenderExtent = result.renderExtent;
                result.fromStreamline = false;
                jitterPhaseCount_ = Context().QueryJitterPhaseCount(result.renderExtent, outputExtent, hdrOutput);
                return result;
            }

            uint32_t JitterPhaseCount() const override { return jitterPhaseCount_; }

            Rendering::Upscaler::FFrameToken BeginFrame(uint32_t frameIndex, bool, uint32_t) override
            {
                return {Context().IsDeviceReady() ? this : nullptr, frameIndex};
            }

            void MarkFrame(Rendering::Upscaler::EFrameMarker,
                           const Rendering::Upscaler::FFrameToken&) override {}
            void SetReflexOptions(bool, uint32_t) override {}
            void ReflexSleep(const Rendering::Upscaler::FFrameToken&) override {}

            bool Evaluate(const Rendering::Upscaler::FFrameInputs& inputs) override
            {
                if (!inputs.enableFSR || inputs.commandBuffer == VK_NULL_HANDLE || inputs.ubo == nullptr ||
                    !inputs.scalingInputColor.IsValid() || !inputs.scalingOutputColor.IsValid() ||
                    !inputs.depth.IsValid() || !inputs.motionVectors.IsValid() ||
                    !Context().EnsureUpscaleContext(inputs.renderExtent, inputs.outputExtent, inputs.hdrOutput))
                {
                    return false;
                }

                ffx::DispatchDescUpscale dispatch{};
                dispatch.commandList = inputs.commandBuffer;
                dispatch.color = ToFfxResource(inputs.scalingInputColor, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
                dispatch.depth = ToFfxResource(inputs.depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
                dispatch.motionVectors = ToFfxResource(inputs.motionVectors, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
                dispatch.output = ToFfxResource(inputs.scalingOutputColor, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
                dispatch.jitterOffset = {-inputs.ubo->Jitter.x, -inputs.ubo->Jitter.y};
                dispatch.motionVectorScale = {1.0f, 1.0f};
                dispatch.renderSize = {inputs.renderExtent.width, inputs.renderExtent.height};
                dispatch.upscaleSize = {inputs.outputExtent.width, inputs.outputExtent.height};
                dispatch.enableSharpening = false;
                dispatch.sharpness = 0.0f;
                dispatch.frameTimeDelta = inputs.frameTimeDeltaMilliseconds;
                dispatch.preExposure = 1.0f;
                dispatch.reset = inputs.reset;
                dispatch.cameraNear = inputs.camera.nearPlane;
                dispatch.cameraFar = inputs.camera.farPlane;
                dispatch.cameraFovAngleVertical = inputs.camera.verticalFovRadians;
                dispatch.viewSpaceToMetersFactor = 1.0f;
                dispatch.flags = inputs.hdrOutput
                    ? FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_PQ
                    : FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB;

                const ffx::ReturnCode result = ffx::Dispatch(Context().UpscaleContext(), dispatch);
                if (result != ffx::ReturnCode::Ok)
                {
                    SPDLOG_ERROR("FidelityFX FSR upscale dispatch failed: {}", static_cast<uint32_t>(result));
                    return false;
                }
                if (!loggedUpscaleDispatch_)
                {
                    SPDLOG_INFO("FidelityFX FSR upscale dispatch is active");
                    loggedUpscaleDispatch_ = true;
                }
                return true;
            }

            void TagFrameGeneration(const Rendering::Upscaler::FFrameInputs& inputs) override
            {
                if (!inputs.enableFSRFrameGeneration || inputs.swapchain == VK_NULL_HANDLE ||
                    inputs.ubo == nullptr || !inputs.hudlessColor.IsValid() ||
                    !Context().EnsureFrameGenerationContext(inputs))
                {
                    return;
                }

                lastSwapchain_ = inputs.swapchain;
                Context().SetLastFrameId(inputs.frameIndex);
                ffx::ConfigureDescFrameGeneration config{};
                config.swapChain = reinterpret_cast<void*>(inputs.swapchain);
                config.frameGenerationCallback = [](ffxDispatchDescFrameGeneration* params, void* userContext)
                {
                    return ffxDispatch(static_cast<ffxContext*>(userContext), &params->header);
                };
                config.frameGenerationCallbackUserContext = &Context().FrameGenerationContext();
                config.frameGenerationEnabled = true;
                config.allowAsyncWorkloads = true;
                config.HUDLessColor = ToFfxResource(inputs.hudlessColor, FFX_API_RESOURCE_STATE_COMPUTE_READ);
                config.generationRect = {inputs.outputOffset.x, inputs.outputOffset.y,
                                         static_cast<int32_t>(inputs.outputExtent.width),
                                         static_cast<int32_t>(inputs.outputExtent.height)};
                config.frameID = inputs.frameIndex;
                if (ffx::Configure(Context().FrameGenerationContext(), config) != ffx::ReturnCode::Ok)
                {
                    SPDLOG_ERROR("FidelityFX Frame Generation configure failed");
                    return;
                }

                ffx::DispatchDescFrameGenerationPrepare prepare{};
                prepare.frameID = inputs.frameIndex;
                prepare.commandList = inputs.commandBuffer;
                prepare.renderSize = {inputs.renderExtent.width, inputs.renderExtent.height};
                prepare.jitterOffset = {-inputs.ubo->Jitter.x, -inputs.ubo->Jitter.y};
                prepare.motionVectorScale = {1.0f, 1.0f};
                prepare.frameTimeDelta = inputs.frameTimeDeltaMilliseconds;
                prepare.cameraNear = inputs.camera.nearPlane;
                prepare.cameraFar = inputs.camera.farPlane;
                prepare.cameraFovAngleVertical = inputs.camera.verticalFovRadians;
                prepare.viewSpaceToMetersFactor = 1.0f;
                prepare.depth = ToFfxResource(inputs.depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
                prepare.motionVectors = ToFfxResource(inputs.motionVectors, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);

                ffx::DispatchDescFrameGenerationPrepareCameraInfo camera{};
                const glm::mat4& inverseView = inputs.ubo->ModelViewInverse;
                const glm::vec3 position = glm::vec3(inverseView[3]);
                const glm::vec3 up = glm::normalize(glm::vec3(inverseView[1]));
                const glm::vec3 right = glm::normalize(glm::vec3(inverseView[0]));
                const glm::vec3 forward = -glm::normalize(glm::vec3(inverseView[2]));
                std::memcpy(camera.cameraPosition, &position, sizeof(position));
                std::memcpy(camera.cameraUp, &up, sizeof(up));
                std::memcpy(camera.cameraRight, &right, sizeof(right));
                std::memcpy(camera.cameraForward, &forward, sizeof(forward));

                const ffx::ReturnCode result = ffx::Dispatch(
                    Context().FrameGenerationContext(), prepare, camera);
                if (result != ffx::ReturnCode::Ok)
                {
                    SPDLOG_ERROR("FidelityFX Frame Generation prepare dispatch failed: {}",
                                  static_cast<uint32_t>(result));
                }
                else if (!loggedFrameGenerationDispatch_)
                {
                    SPDLOG_INFO("FidelityFX Frame Generation prepare dispatch is active");
                    loggedFrameGenerationDispatch_ = true;
                }
            }

            void UpdateFrameGenerationState() override
            {
                frameGenerationState_.valid = lastSwapchain_ != VK_NULL_HANDLE;
                frameGenerationState_.numFramesActuallyPresented =
                    frameGenerationState_.valid ? 2u : 0u;
                frameGenerationState_.numFramesToGenerateMax = 1;
                frameGenerationState_.statusMask = 0;
            }

            Rendering::Upscaler::FFrameGenerationState FrameGenerationState() const override
            {
                return frameGenerationState_;
            }

        private:
            VkSwapchainKHR lastSwapchain_ = VK_NULL_HANDLE;
            uint32_t jitterPhaseCount_ = 0;
            bool loggedUpscaleDispatch_ = false;
            bool loggedFrameGenerationDispatch_ = false;
            Rendering::Upscaler::FFrameGenerationState frameGenerationState_{};
        };
    }

    Vulkan::IVulkanInterposer& InterposerInstance()
    {
        static FFidelityFXInterposer interposer;
        return interposer;
    }

    Vulkan::IDeviceCreationAugmenter& DeviceAugmenterInstance()
    {
        static FFidelityFXDeviceAugmenter augmenter;
        return augmenter;
    }
#endif

    std::unique_ptr<Rendering::Upscaler::IUpscaler> CreateUpscaler()
    {
#if WITH_FIDELITYFX && WIN32
        return std::make_unique<FFidelityFXUpscaler>();
#else
        return nullptr;
#endif
    }
}
