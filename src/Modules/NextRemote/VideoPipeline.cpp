#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/NextRemote/VideoPipeline.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

#include <algorithm>
#include <utility>

#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
    namespace
    {
        constexpr size_t maxGpuEncodeSlots = 4;

        // The convert shader stores packed 4-byte words into every plane, which requires the
        // destination width to be a multiple of 8 (chroma rows are width/2).
        uint32_t AlignWidth(uint32_t value, uint32_t fallback)
        {
            const uint32_t selected = value == 0 ? fallback : value;
            return std::max(8u, selected & ~7u);
        }

        uint32_t AlignHeight(uint32_t value, uint32_t fallback)
        {
            const uint32_t selected = value == 0 ? fallback : value;
            return std::max(2u, selected & ~1u);
        }

        std::chrono::steady_clock::duration FrameInterval(uint32_t fps)
        {
            return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(std::max(1u, fps))));
        }

        void InsertVideoPipelineImageBarrier(VkCommandBuffer commandBuffer, VkImage image, VkAccessFlags srcAccessMask,
                                             VkAccessFlags dstAccessMask, VkImageLayout oldLayout,
                                             VkImageLayout newLayout, VkImageAspectFlags aspectMask,
                                             VkPipelineStageFlags srcStageMask,
                                             VkPipelineStageFlags dstStageMask)
        {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = srcAccessMask;
            barrier.dstAccessMask = dstAccessMask;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image;
            barrier.subresourceRange.aspectMask = aspectMask;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }

        uint32_t FindMemoryType(const Vulkan::Device& device, uint32_t typeBits, VkMemoryPropertyFlags properties)
        {
            VkPhysicalDeviceMemoryProperties memoryProperties{};
            vkGetPhysicalDeviceMemoryProperties(device.PhysicalDevice(), &memoryProperties);
            for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
            {
                if (((typeBits & (1u << i)) != 0) &&
                    (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties)
                {
                    return i;
                }
            }
            throw std::runtime_error("failed to find matching Vulkan memory type for remote encode image");
        }

        // Must match PushConsts in assets/shaders/Remote.BgraToNv12.comp.slang.
        struct FGpuConvertPushConsts
        {
            uint32_t srcIndex;
            uint32_t outYIndex;
            uint32_t outUvIndex;
            uint32_t dstWidth;
            uint32_t dstHeight;
            uint32_t srcWidth;
            uint32_t srcHeight;
            uint32_t pad0;
        };
    }

    FVideoPipeline::FVideoPipeline(RemoteServer::FConfig config)
        : config_(std::move(config))
        , dstWidth_(AlignWidth(config_.width, 640u))
        , dstHeight_(AlignHeight(config_.height, 360u))
        , requestedEncoder_(config_.encoderBackend)
        , requestedEncoderName_(ToString(config_.encoderBackend))
        , desiredBitrateKbps_(std::max(1u, config_.bitrateKbps))
        , targetFps_(std::max(1u, config_.fps))
    {
    }

    FVideoPipeline::~FVideoPipeline()
    {
        Stop();
    }

    bool FVideoPipeline::Initialize(Vulkan::VulkanBaseRenderer& renderer)
    {
        if (initialized_)
        {
            return true;
        }

        device_ = &renderer.Device();
        vulkanCaps_ = renderer.VideoCaps();
        {
            std::lock_guard lock(profileMutex_);
            RecomputeNegotiatedProfileLocked();
        }

        if (!vulkanCaps_ || !vulkanCaps_->Usable())
        {
            SPDLOG_ERROR("RemotePlay: Vulkan Video H.264 encode is unavailable on this device; remote mode is disabled");
            return false;
        }

        try
        {
            encoder_ = std::make_unique<FVulkanVideoEncoder>(renderer.Device(), *vulkanCaps_, BuildEncoderConfig());
        }
        catch (const std::exception& error)
        {
            SPDLOG_ERROR("RemotePlay: failed to create Vulkan Video encoder: {}", error.what());
            encoder_.reset();
            return false;
        }

        activeEncoderName_ = encoder_->Name();
        initialized_ = true;
        SPDLOG_INFO("RemotePlay: video pipeline backend = {}", activeEncoderName_);
        return true;
    }

    void FVideoPipeline::Start()
    {
        if (encodeThread_.joinable() || !initialized_ || !encoder_)
        {
            return;
        }
        startedAt_ = std::chrono::steady_clock::now();
        nextFrameTime_ = startedAt_;
        encodeStopRequested_.store(false);
        encodeThread_ = std::thread([this]() { EncodeLoop(); });
    }

    void FVideoPipeline::Stop()
    {
        if (encodeThread_.joinable())
        {
            encodeStopRequested_.store(true);
            encodeCv_.notify_all();
            encodeThread_.join();
        }
        {
            std::lock_guard lock(encodeQueueMutex_);
            encodeQueue_.clear();
        }
        if (encoder_)
        {
            encoder_->Stop();
        }

        // Convert dispatches may still be in flight inside the renderer's frame command buffers.
        if (device_ && !slots_.empty())
        {
            device_->WaitIdle();
        }
        gpuConvertPipeline_.reset();
        graphicsCompletionSemaphore_.reset();
        for (auto& slot : slots_)
        {
            DestroySlotResources(*slot);
        }
        slots_.clear();
        device_ = nullptr;
        initialized_ = false;
        encoder_.reset();
    }

    void FVideoPipeline::ReleaseSwapChainResources()
    {
        gpuConvertPipeline_.reset();
    }

    uint64_t FVideoPipeline::AddSink(FPacketSink sink)
    {
        uint64_t id = 0;
        {
            std::lock_guard lock(sinksMutex_);
            id = nextSinkId_++;
            sinks_[id] = std::move(sink);
            sinkCount_.store(static_cast<uint32_t>(sinks_.size()), std::memory_order_relaxed);
        }
        RequestKeyframe();
        return id;
    }

    void FVideoPipeline::RemoveSink(uint64_t sinkId)
    {
        std::lock_guard lock(sinksMutex_);
        sinks_.erase(sinkId);
        sinkCount_.store(static_cast<uint32_t>(sinks_.size()), std::memory_order_relaxed);
    }

    void FVideoPipeline::RequestKeyframe()
    {
        keyframeRequested_.store(true, std::memory_order_relaxed);
    }

    void FVideoPipeline::SetBitrate(uint32_t bitrateKbps)
    {
        const uint32_t clamped = std::max(1u, bitrateKbps);
        desiredBitrateKbps_.store(clamped, std::memory_order_relaxed);
    }

    void FVideoPipeline::SetTargetFps(uint32_t fps)
    {
        targetFps_.store(std::max(1u, fps), std::memory_order_relaxed);
    }

    FVideoPipeline::FStats FVideoPipeline::Stats() const
    {
        FStats stats;
        stats.sinkCount = sinkCount_.load(std::memory_order_relaxed);
        stats.droppedFrames = droppedFrames_.load(std::memory_order_relaxed);
        stats.bitrateKbps = desiredBitrateKbps_.load(std::memory_order_relaxed);
        stats.targetFps = targetFps_.load(std::memory_order_relaxed);
        stats.activeEncoder = activeEncoderName_;
        return stats;
    }

    void FVideoPipeline::RegisterClientH264Profiles(std::string sessionId, uint32_t profileMask)
    {
        std::lock_guard lock(profileMutex_);
        clientH264Profiles_[std::move(sessionId)] =
            (profileMask & h264ProfileAllBits) != 0 ? (profileMask & h264ProfileAllBits) : h264ProfileBaselineBit;
        RecomputeNegotiatedProfileLocked();
    }

    void FVideoPipeline::UnregisterClientH264Profiles(const std::string& sessionId)
    {
        std::lock_guard lock(profileMutex_);
        clientH264Profiles_.erase(sessionId);
        if (clientH264Profiles_.empty())
        {
            profileFrozen_ = false;
            negotiatedProfileIdc_ = STD_VIDEO_H264_PROFILE_IDC_BASELINE;
        }
    }

    std::string FVideoPipeline::OfferH264FmtpLine() const
    {
        std::lock_guard lock(profileMutex_);
        return BuildH264FmtpLine(ActiveH264ProfileIdcLocked(), dstWidth_, dstHeight_, std::max(1u, config_.fps));
    }

    FVideoEncoderConfig FVideoPipeline::BuildEncoderConfig() const
    {
        FVideoEncoderConfig encoderConfig;
        encoderConfig.width = dstWidth_;
        encoderConfig.height = dstHeight_;
        encoderConfig.fps = std::max(1u, config_.fps);
        encoderConfig.bitrateKbps = desiredBitrateKbps_.load(std::memory_order_relaxed);
        {
            std::lock_guard lock(profileMutex_);
            encoderConfig.h264ProfileIdc = ActiveH264ProfileIdcLocked();
        }
        return encoderConfig;
    }

    int32_t FVideoPipeline::ActiveH264ProfileIdcLocked() const
    {
        return negotiatedProfileIdc_;
    }

    void FVideoPipeline::RecomputeNegotiatedProfileLocked()
    {
        if (profileFrozen_ && !clientH264Profiles_.empty())
        {
            return;
        }

        uint32_t clientMask = h264ProfileAllBits;
        if (!clientH264Profiles_.empty())
        {
            for (const auto& [id, mask] : clientH264Profiles_)
            {
                clientMask &= mask;
            }
            if (clientMask == 0)
            {
                clientMask = h264ProfileBaselineBit;
            }
        }

        uint32_t encoderMask =
            (vulkanCaps_ && vulkanCaps_->supportedProfileMask != 0) ? vulkanCaps_->supportedProfileMask
                                                                    : h264ProfileBaselineBit;

        uint32_t selectedMask = clientMask & encoderMask;
        if (selectedMask == 0)
        {
            selectedMask = (encoderMask & h264ProfileBaselineBit) != 0 ? h264ProfileBaselineBit : encoderMask;
        }

        const int32_t newProfileIdc = ChoosePreferredStdProfileIdc(selectedMask);
        if (newProfileIdc != negotiatedProfileIdc_)
        {
            negotiatedProfileIdc_ = newProfileIdc;
            if (initialized_ && activeEncoderName_ == std::string_view("vulkan"))
            {
                recreateEncoderRequested_.store(true, std::memory_order_relaxed);
                keyframeRequested_.store(true, std::memory_order_relaxed);
            }
            SPDLOG_INFO("RemotePlay: negotiated H.264 profile = {}", H264ProfileNameFromStdProfileIdc(newProfileIdc));
        }

        if (!profileFrozen_ && !clientH264Profiles_.empty())
        {
            profileFrozen_ = true;
        }
    }

    void FVideoPipeline::HarvestCompletedSlots(uint64_t currentFrameId)
    {
        // DrawFrame waits the previous submit fence before recording the current frame, so every
        // capture recorded at frameId < currentFrameId is guaranteed GPU-complete by now.
        for (size_t i = 0; i < slots_.size(); ++i)
        {
            FSlot& slot = *slots_[i];
            if (slot.state.load(std::memory_order_acquire) == ESlotState::Recorded && slot.frameId < currentFrameId)
            {
                slot.state.store(ESlotState::Encoding, std::memory_order_release);
                {
                    std::lock_guard lock(encodeQueueMutex_);
                    encodeQueue_.push_back(i);
                }
                encodeCv_.notify_one();
            }
        }
    }

    void FVideoPipeline::RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                     Vulkan::VulkanBaseRenderer& renderer)
    {
        const Vulkan::SwapChain& swapChain = renderer.SwapChain();
        if (swapChain.IsHDR())
        {
            if (!warnedHdr_)
            {
                warnedHdr_ = true;
                SPDLOG_WARN("RemotePlay: HDR swapchain is not supported by the video pipeline; no video frames");
            }
            return;
        }

        const VkImage swapImage = swapChain.Images()[imageIndex];
        const VkExtent2D srcExtent = swapChain.Extent();
        if (srcExtent.width == 0 || srcExtent.height == 0)
        {
            return;
        }

        RecordFrameFromSource(commandBuffer, imageIndex, renderer, Assets::Bindless::RT_SWAPCHAIN0 + imageIndex,
                              srcExtent, swapImage);
    }

    void FVideoPipeline::RecordFrameFromStorage(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                                Vulkan::VulkanBaseRenderer& renderer,
                                                const uint32_t sourceBindlessIndex,
                                                const VkExtent2D sourceExtent)
    {
        if (const Vulkan::RenderImage* sourceImage = renderer.GetStorageImage(sourceBindlessIndex))
        {
            RecordFrameFromStorageImage(commandBuffer,
                                        imageIndex,
                                        renderer,
                                        *sourceImage,
                                        sourceBindlessIndex,
                                        sourceExtent,
                                        VK_ACCESS_SHADER_WRITE_BIT,
                                        VK_ACCESS_SHADER_WRITE_BIT);
        }
    }

    void FVideoPipeline::RecordFrameFromStorageImage(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                                     Vulkan::VulkanBaseRenderer& renderer,
                                                     const Vulkan::RenderImage& sourceImage,
                                                     const uint32_t sourceBindlessIndex,
                                                     const VkExtent2D sourceExtent,
                                                     const VkAccessFlags sourceAccessBefore,
                                                     const VkAccessFlags sourceAccessAfter)
    {
        sourceImage.InsertBarrier(commandBuffer, sourceAccessBefore, VK_ACCESS_SHADER_READ_BIT,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        RecordFrameFromSource(commandBuffer, imageIndex, renderer, sourceBindlessIndex, sourceExtent, VK_NULL_HANDLE);
        sourceImage.InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, sourceAccessAfter,
                                  VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void FVideoPipeline::RecordFrameFromSource(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                               Vulkan::VulkanBaseRenderer& renderer,
                                               const uint32_t sourceBindlessIndex,
                                               const VkExtent2D sourceExtent,
                                               const VkImage swapChainImageForLegacyRestore)
    {
        if (!initialized_ || !encoder_)
        {
            return;
        }

        if (slots_.empty())
        {
            size_t slotCount = std::max<size_t>(renderer.SwapChain().Images().size(), 3);
            if (slotCount > maxGpuEncodeSlots)
            {
                SPDLOG_WARN("RemotePlay: clamping GPU encode slot count from {} to {} for bindless NV12 staging",
                            slotCount, maxGpuEncodeSlots);
                slotCount = maxGpuEncodeSlots;
            }
            slots_.reserve(slotCount);
            for (size_t i = 0; i < slotCount; ++i)
            {
                slots_.push_back(std::make_unique<FSlot>());
            }
        }

        const auto currentFrameId = static_cast<uint64_t>(renderer.FrameCount());
        HarvestCompletedSlots(currentFrameId);

        if (sinkCount_.load(std::memory_order_relaxed) == 0)
        {
            return;
        }

        if (!graphicsCompletionSemaphore_)
        {
            graphicsCompletionSemaphore_ = std::make_unique<Vulkan::TimelineSemaphore>(renderer.Device(), 0);
            useTimelineCompletion_ = true;
            SPDLOG_INFO("RemotePlay: video pipeline armed graphics timeline completion semaphore");
        }

        const auto now = std::chrono::steady_clock::now();
        if (now < nextFrameTime_)
        {
            return;
        }

        const auto interval = FrameInterval(targetFps_.load(std::memory_order_relaxed));
        const auto advanceThrottle = [&]()
        {
            nextFrameTime_ += interval;
            if (nextFrameTime_ + interval < now)
            {
                nextFrameTime_ = now + interval;
            }
        };

        FSlot* freeSlot = nullptr;
        size_t freeSlotIndex = 0;
        for (size_t slotIndex = 0; slotIndex < slots_.size(); ++slotIndex)
        {
            if (slots_[slotIndex]->state.load(std::memory_order_acquire) == ESlotState::Free)
            {
                freeSlot = slots_[slotIndex].get();
                freeSlotIndex = slotIndex;
                break;
            }
        }
        if (!freeSlot)
        {
            // Drop this frame instead of back-pressuring the renderer; keep the throttle advancing
            // so each missed capture slot counts once.
            advanceThrottle();
            const uint64_t droppedFrames = droppedFrames_.fetch_add(1, std::memory_order_relaxed) + 1;
            if ((droppedFrames & (droppedFrames - 1)) == 0)
            {
                SPDLOG_WARN("RemotePlay: encoder backlog, dropped {} capture frames so far", droppedFrames);
            }
            return;
        }

        if (!EnsureSlotResources(*freeSlot, renderer, freeSlotIndex))
        {
            return;
        }

        if (sourceExtent.width == 0 || sourceExtent.height == 0)
        {
            return;
        }

        if (swapChainImageForLegacyRestore != VK_NULL_HANDLE)
        {
            Vulkan::ImageMemoryBarrier::FullInsert(commandBuffer, swapChainImageForLegacyRestore, 0,
                                                   VK_ACCESS_SHADER_READ_BIT,
                                                   VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);
        }

        if (!gpuConvertPipeline_)
        {
            gpuConvertPipeline_ = std::make_unique<Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline>(
                renderer.SwapChain(), "assets/shaders/Remote.BgraToNv12.comp.slang.spv",
                static_cast<uint32_t>(sizeof(FGpuConvertPushConsts)));
        }

        freeSlot->yPlane->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, freeSlot->yPlaneLayout,
                                        VK_IMAGE_LAYOUT_GENERAL);
        freeSlot->uvPlane->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, freeSlot->uvPlaneLayout,
                                         VK_IMAGE_LAYOUT_GENERAL);
        freeSlot->yPlaneLayout = VK_IMAGE_LAYOUT_GENERAL;
        freeSlot->uvPlaneLayout = VK_IMAGE_LAYOUT_GENERAL;

        FGpuConvertPushConsts pushConsts{};
        pushConsts.srcIndex = sourceBindlessIndex;
        pushConsts.outYIndex = freeSlot->yBindlessIndex;
        pushConsts.outUvIndex = freeSlot->uvBindlessIndex;
        pushConsts.dstWidth = dstWidth_;
        pushConsts.dstHeight = dstHeight_;
        pushConsts.srcWidth = sourceExtent.width;
        pushConsts.srcHeight = sourceExtent.height;
        gpuConvertPipeline_->BindPipeline(commandBuffer, &pushConsts);
        vkCmdDispatch(commandBuffer, (dstWidth_ + 7) / 8, (dstHeight_ + 7) / 8, 1);

        freeSlot->yPlane->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                        freeSlot->yPlaneLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        freeSlot->uvPlane->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                                         freeSlot->uvPlaneLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        freeSlot->yPlaneLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        freeSlot->uvPlaneLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        const VkAccessFlags encodeSrcAccess = freeSlot->encodeImage.layout == VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR
                                                  ? VK_ACCESS_MEMORY_READ_BIT
                                                  : 0;
        const VkPipelineStageFlags encodeSrcStage =
            freeSlot->encodeImage.layout == VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR
                ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
                : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        InsertVideoPipelineImageBarrier(commandBuffer, freeSlot->encodeImage.image, encodeSrcAccess,
                                        VK_ACCESS_TRANSFER_WRITE_BIT, freeSlot->encodeImage.layout,
                                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                        VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT, encodeSrcStage,
                                        VK_PIPELINE_STAGE_TRANSFER_BIT);
        freeSlot->encodeImage.layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        VkImageCopy lumaCopy{};
        lumaCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        lumaCopy.srcSubresource.layerCount = 1;
        lumaCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
        lumaCopy.dstSubresource.layerCount = 1;
        lumaCopy.extent = {dstWidth_, dstHeight_, 1};
        vkCmdCopyImage(commandBuffer, freeSlot->yPlane->GetImage().Handle(), freeSlot->yPlaneLayout,
                       freeSlot->encodeImage.image, freeSlot->encodeImage.layout, 1, &lumaCopy);

        VkImageCopy chromaCopy{};
        chromaCopy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        chromaCopy.srcSubresource.layerCount = 1;
        chromaCopy.dstSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
        chromaCopy.dstSubresource.layerCount = 1;
        chromaCopy.extent = {dstWidth_ / 2u, dstHeight_ / 2u, 1};
        vkCmdCopyImage(commandBuffer, freeSlot->uvPlane->GetImage().Handle(), freeSlot->uvPlaneLayout,
                       freeSlot->encodeImage.image, freeSlot->encodeImage.layout, 1, &chromaCopy);

        freeSlot->yPlane->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                                        freeSlot->yPlaneLayout, VK_IMAGE_LAYOUT_GENERAL);
        freeSlot->uvPlane->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                                         freeSlot->uvPlaneLayout, VK_IMAGE_LAYOUT_GENERAL);
        freeSlot->yPlaneLayout = VK_IMAGE_LAYOUT_GENERAL;
        freeSlot->uvPlaneLayout = VK_IMAGE_LAYOUT_GENERAL;

        InsertVideoPipelineImageBarrier(commandBuffer, freeSlot->encodeImage.image, VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                                        freeSlot->encodeImage.layout, VK_IMAGE_LAYOUT_GENERAL,
                                        VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT,
                                        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        freeSlot->encodeImage.layout = VK_IMAGE_LAYOUT_GENERAL;

        if (swapChainImageForLegacyRestore != VK_NULL_HANDLE)
        {
            Vulkan::ImageMemoryBarrier::FullInsert(commandBuffer, swapChainImageForLegacyRestore,
                                                   VK_ACCESS_SHADER_READ_BIT, 0,
                                                   VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
        // Buffer visibility to the host is guaranteed by the frame fence + HOST_COHERENT memory.

        freeSlot->frameId = currentFrameId;
        freeSlot->captureTimestampUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - startedAt_).count());
        if (useTimelineCompletion_ && graphicsCompletionSemaphore_)
        {
            freeSlot->completionValue = nextCompletionValue_++;
            renderer.QueueSubmitSignalSemaphore(graphicsCompletionSemaphore_->Handle(), freeSlot->completionValue);
            freeSlot->state.store(ESlotState::Encoding, std::memory_order_release);
            {
                std::lock_guard lock(encodeQueueMutex_);
                encodeQueue_.push_back(freeSlotIndex);
            }
            encodeCv_.notify_one();
        }
        else
        {
            freeSlot->completionValue = 0;
            freeSlot->state.store(ESlotState::Recorded, std::memory_order_release);
        }

        advanceThrottle();
    }

    bool FVideoPipeline::EnsureSlotResources(FSlot& slot, Vulkan::VulkanBaseRenderer& renderer, size_t slotIndex)
    {
        return EnsureGpuSlotResources(slot, renderer, slotIndex);
    }

    bool FVideoPipeline::CreateGpuEncodeImage(FSlot& slot, const Vulkan::Device& device, size_t slotIndex)
    {
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        imageInfo.extent = {dstWidth_, dstHeight_, 1};
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

        const uint32_t videoEncodeFamilyIndex = device.VideoEncodeFamilyIndex();
        std::array<uint32_t, 2> queueFamilies{device.GraphicsFamilyIndex(), videoEncodeFamilyIndex};
        if (videoEncodeFamilyIndex != UINT32_MAX && videoEncodeFamilyIndex != device.GraphicsFamilyIndex())
        {
            imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            imageInfo.queueFamilyIndexCount = static_cast<uint32_t>(queueFamilies.size());
            imageInfo.pQueueFamilyIndices = queueFamilies.data();
        }
        else
        {
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        Vulkan::Check(vkCreateImage(device.Handle(), &imageInfo, nullptr, &slot.encodeImage.image),
                      "create remote GPU encode image");

        VkMemoryRequirements memoryRequirements{};
        vkGetImageMemoryRequirements(device.Handle(), slot.encodeImage.image, &memoryRequirements);

        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = memoryRequirements.size;
        allocateInfo.memoryTypeIndex =
            FindMemoryType(device, memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        Vulkan::Check(vkAllocateMemory(device.Handle(), &allocateInfo, nullptr, &slot.encodeImage.memory),
                      "allocate remote GPU encode image memory");
        Vulkan::Check(vkBindImageMemory(device.Handle(), slot.encodeImage.image, slot.encodeImage.memory, 0),
                      "bind remote GPU encode image memory");

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = slot.encodeImage.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        Vulkan::Check(vkCreateImageView(device.Handle(), &viewInfo, nullptr, &slot.encodeImage.view),
                      "create remote GPU encode image view");

        slot.encodeImage.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        const auto& debugUtils = device.DebugUtils();
        const std::string imageName = fmt::format("RemoteEncodeImage{}", slotIndex);
        const std::string viewName = fmt::format("RemoteEncodeImageView{}", slotIndex);
        debugUtils.SetObjectName(slot.encodeImage.image, imageName.c_str());
        debugUtils.SetObjectName(slot.encodeImage.view, viewName.c_str());
        return true;
    }

    bool FVideoPipeline::EnsureGpuSlotResources(FSlot& slot, Vulkan::VulkanBaseRenderer& renderer, size_t slotIndex)
    {
        if (slot.yPlane && slot.uvPlane && slot.encodeImage.view != VK_NULL_HANDLE)
        {
            return true;
        }

        device_ = &renderer.Device();
        auto* texturePool = Assets::GlobalTexturePool::GetInstance();
        if (!texturePool)
        {
            SPDLOG_ERROR("RemotePlay: GlobalTexturePool unavailable for GPU encode path");
            return false;
        }

        try
        {
            const VkExtent2D lumaExtent{dstWidth_, dstHeight_};
            const VkExtent2D chromaExtent{dstWidth_ / 2u, dstHeight_ / 2u};
            slot.yPlane = std::make_unique<Vulkan::RenderImage>(
                renderer.Device(), lumaExtent, VK_FORMAT_R8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false,
                fmt::format("RemoteEncodeY{}", slotIndex).c_str());
            slot.uvPlane = std::make_unique<Vulkan::RenderImage>(
                renderer.Device(), chromaExtent, VK_FORMAT_R8G8_UNORM, VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false,
                fmt::format("RemoteEncodeUV{}", slotIndex).c_str());
            slot.yPlaneLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            slot.uvPlaneLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            slot.yBindlessIndex = config_.encodeBindlessBase + static_cast<uint32_t>(slotIndex) * 2u;
            slot.uvBindlessIndex = slot.yBindlessIndex + 1u;
            texturePool->BindStorageTexture(slot.yBindlessIndex, slot.yPlane->GetImageView());
            texturePool->BindStorageTexture(slot.uvBindlessIndex, slot.uvPlane->GetImageView());
            return CreateGpuEncodeImage(slot, renderer.Device(), slotIndex);
        }
        catch (const std::exception& error)
        {
            SPDLOG_ERROR("RemotePlay: failed to allocate GPU encode slot {}: {}", slotIndex, error.what());
            DestroyGpuSlotResources(slot);
            return false;
        }
    }

    void FVideoPipeline::DestroySlotResources(FSlot& slot)
    {
        DestroyGpuSlotResources(slot);
        slot.state.store(ESlotState::Free, std::memory_order_release);
    }

    void FVideoPipeline::DestroyGpuSlotResources(FSlot& slot)
    {
        slot.yPlane.reset();
        slot.uvPlane.reset();
        slot.yPlaneLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        slot.uvPlaneLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (device_)
        {
            if (slot.encodeImage.view != VK_NULL_HANDLE)
            {
                vkDestroyImageView(device_->Handle(), slot.encodeImage.view, nullptr);
            }
            if (slot.encodeImage.image != VK_NULL_HANDLE)
            {
                vkDestroyImage(device_->Handle(), slot.encodeImage.image, nullptr);
            }
            if (slot.encodeImage.memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(device_->Handle(), slot.encodeImage.memory, nullptr);
            }
        }
        slot.encodeImage = {};
    }

    void FVideoPipeline::EncodeLoop()
    {
        uint64_t sentFrames = 0;
        uint32_t appliedBitrateKbps = desiredBitrateKbps_.load(std::memory_order_relaxed);
        while (!encodeStopRequested_.load())
        {
            size_t slotIndex = 0;
            {
                std::unique_lock lock(encodeQueueMutex_);
                encodeCv_.wait(lock, [this]()
                {
                    return encodeStopRequested_.load() || !encodeQueue_.empty();
                });
                if (encodeStopRequested_.load())
                {
                    break;
                }
                slotIndex = encodeQueue_.front();
                encodeQueue_.pop_front();
            }

            FSlot& slot = *slots_[slotIndex];
            if (sinkCount_.load(std::memory_order_relaxed) == 0 || !encoder_ ||
                slot.encodeImage.view == VK_NULL_HANDLE)
            {
                slot.state.store(ESlotState::Free, std::memory_order_release);
                continue;
            }

            const uint64_t timestampUs = slot.captureTimestampUs;

            if (useTimelineCompletion_ && graphicsCompletionSemaphore_ && slot.completionValue != 0)
            {
                graphicsCompletionSemaphore_->Wait(slot.completionValue);
            }

            if (recreateEncoderRequested_.exchange(false, std::memory_order_relaxed) &&
                activeEncoderName_ == std::string_view("vulkan") && device_ && vulkanCaps_)
            {
                encoder_->Stop();
                encoder_ = std::make_unique<FVulkanVideoEncoder>(*device_, *vulkanCaps_, BuildEncoderConfig());
                SPDLOG_INFO("RemotePlay: recreated Vulkan encoder with H.264 profile {}",
                            H264ProfileNameFromStdProfileIdc(BuildEncoderConfig().h264ProfileIdc));
            }
            const uint32_t desiredBitrateKbps = desiredBitrateKbps_.load(std::memory_order_relaxed);
            if (desiredBitrateKbps != appliedBitrateKbps)
            {
                encoder_->SetBitrate(desiredBitrateKbps);
                appliedBitrateKbps = desiredBitrateKbps;
                SPDLOG_INFO("RemotePlay: encoder bitrate updated to {}kbps", appliedBitrateKbps);
            }

            if (keyframeRequested_.exchange(false, std::memory_order_relaxed))
            {
                encoder_->RequestKeyframe();
            }

            std::vector<std::byte> encodedFrame;
            bool keyframe = false;
            FGpuVideoFrame frame;
            frame.image = slot.encodeImage.image;
            frame.view = slot.encodeImage.view;
            frame.layout = &slot.encodeImage.layout;
            frame.width = dstWidth_;
            frame.height = dstHeight_;
            const bool encoded = encoder_->EncodeGpu(frame, timestampUs / 1000u, encodedFrame, keyframe);
            slot.state.store(ESlotState::Free, std::memory_order_release);
            if (!encoded)
            {
                continue;
            }

            FEncodedPacket packet;
            packet.annexb = std::make_shared<const std::vector<std::byte>>(std::move(encodedFrame));
            packet.keyframe = keyframe;
            packet.timestampUs = timestampUs;
            Broadcast(packet);

            ++sentFrames;
            if (sentFrames == 1 || sentFrames % std::max(1u, config_.fps * 5u) == 0)
            {
                SPDLOG_INFO("RemotePlay: encoded frame #{} {} bytes keyframe={}", sentFrames, packet.annexb->size(),
                            packet.keyframe);
            }
        }
    }

    void FVideoPipeline::Broadcast(const FEncodedPacket& packet)
    {
        std::lock_guard lock(sinksMutex_);
        for (const auto& [id, sink] : sinks_)
        {
            sink(packet);
        }
    }
}
