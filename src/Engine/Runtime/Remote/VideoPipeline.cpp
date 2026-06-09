#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Remote/VideoPipeline.hpp"

#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

namespace Runtime::Remote
{
    namespace
    {
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

        // Must match PushConsts in assets/shaders/Remote.BgraToYuv.comp.slang.
        struct FConvertPushConsts
        {
            uint64_t outAddress;
            uint32_t swapChainIndex;
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
        , encoder_({dstWidth_, dstHeight_, std::max(1u, config_.fps), std::max(1u, config_.bitrateKbps)})
    {
    }

    FVideoPipeline::~FVideoPipeline()
    {
        Stop();
    }

    void FVideoPipeline::Start()
    {
        if (encodeThread_.joinable())
        {
            return;
        }
        startedAt_ = std::chrono::steady_clock::now();
        nextFrameTime_ = startedAt_;
        encodeThread_ = std::jthread([this](const std::stop_token& stopToken) { EncodeLoop(stopToken); });
    }

    void FVideoPipeline::Stop()
    {
        if (encodeThread_.joinable())
        {
            encodeThread_.request_stop();
            encodeCv_.notify_all();
            encodeThread_.join();
        }
        encoder_.Stop();

        // Convert dispatches may still be in flight inside the renderer's frame command buffers.
        if (device_ && !slots_.empty())
        {
            device_->WaitIdle();
        }
        convertPipeline_.reset();
        for (auto& slot : slots_)
        {
            DestroySlotResources(*slot);
        }
        slots_.clear();
        device_ = nullptr;
    }

    void FVideoPipeline::ReleaseSwapChainResources()
    {
        convertPipeline_.reset();
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
        if (slots_.empty())
        {
            const size_t slotCount = std::max<size_t>(renderer.SwapChain().Images().size(), 3);
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

        const auto now = std::chrono::steady_clock::now();
        if (now < nextFrameTime_)
        {
            return;
        }

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

        const auto interval = FrameInterval(config_.fps);
        const auto advanceThrottle = [&]()
        {
            nextFrameTime_ += interval;
            if (nextFrameTime_ + interval < now)
            {
                nextFrameTime_ = now + interval;
            }
        };

        FSlot* freeSlot = nullptr;
        for (auto& slot : slots_)
        {
            if (slot->state.load(std::memory_order_acquire) == ESlotState::Free)
            {
                freeSlot = slot.get();
                break;
            }
        }
        if (!freeSlot)
        {
            // Drop this frame instead of back-pressuring the renderer; keep the throttle advancing
            // so each missed capture slot counts once.
            advanceThrottle();
            ++droppedFrames_;
            if ((droppedFrames_ & (droppedFrames_ - 1)) == 0)
            {
                SPDLOG_WARN("RemotePlay: encoder backlog, dropped {} capture frames so far", droppedFrames_);
            }
            return;
        }

        if (!EnsureSlotResources(*freeSlot, renderer))
        {
            return;
        }

        if (!convertPipeline_)
        {
            convertPipeline_ = std::make_unique<Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline>(
                swapChain, "assets/shaders/Remote.BgraToYuv.comp.slang.spv",
                static_cast<uint32_t>(sizeof(FConvertPushConsts)));
        }

        const VkImage swapImage = swapChain.Images()[imageIndex];
        const VkExtent2D srcExtent = swapChain.Extent();
        if (srcExtent.width == 0 || srcExtent.height == 0)
        {
            return;
        }

        Vulkan::ImageMemoryBarrier::FullInsert(commandBuffer, swapImage, 0, VK_ACCESS_SHADER_READ_BIT,
                                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);

        FConvertPushConsts pushConsts{};
        pushConsts.outAddress = freeSlot->address;
        pushConsts.swapChainIndex = imageIndex;
        pushConsts.dstWidth = dstWidth_;
        pushConsts.dstHeight = dstHeight_;
        pushConsts.srcWidth = srcExtent.width;
        pushConsts.srcHeight = srcExtent.height;
        convertPipeline_->BindPipeline(commandBuffer, &pushConsts);

        // Thread = 8x2 destination pixels, group = 8x8 threads -> 64x16 pixels per group.
        vkCmdDispatch(commandBuffer, (dstWidth_ + 63) / 64, (dstHeight_ + 15) / 16, 1);

        Vulkan::ImageMemoryBarrier::FullInsert(commandBuffer, swapImage, VK_ACCESS_SHADER_READ_BIT, 0,
                                               VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        // Buffer visibility to the host is guaranteed by the frame fence + HOST_COHERENT memory.

        freeSlot->frameId = currentFrameId;
        freeSlot->captureTimestampUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - startedAt_).count());
        freeSlot->state.store(ESlotState::Recorded, std::memory_order_release);

        advanceThrottle();
    }

    bool FVideoPipeline::EnsureSlotResources(FSlot& slot, Vulkan::VulkanBaseRenderer& renderer)
    {
        if (slot.buffer)
        {
            return true;
        }

        device_ = &renderer.Device();
        const size_t bufferSize = static_cast<size_t>(dstWidth_) * dstHeight_ * 3u / 2u;

        // The encoder thread reads this memory every frame: prefer HOST_CACHED readback memory,
        // because reads from write-combined (coherent-only) memory are an order of magnitude slower.
        try
        {
            Vulkan::BufferUtil::CreateDeviceBufferLocal(
                renderer.CommandPool(), "RemoteVideo I420", VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                    VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                bufferSize, slot.buffer, slot.memory);
        }
        catch (const std::exception&)
        {
            Vulkan::BufferUtil::CreateDeviceBufferLocal(
                renderer.CommandPool(), "RemoteVideo I420", VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, bufferSize, slot.buffer,
                slot.memory);
        }

        slot.address = slot.buffer->GetDeviceAddress();
        slot.mapped = static_cast<uint8_t*>(slot.memory->Map(0, bufferSize));
        return slot.mapped != nullptr;
    }

    void FVideoPipeline::DestroySlotResources(FSlot& slot)
    {
        if (slot.mapped && slot.memory)
        {
            slot.memory->Unmap();
        }
        slot.mapped = nullptr;
        slot.address = 0;
        slot.buffer.reset();
        slot.memory.reset();
        slot.state.store(ESlotState::Free, std::memory_order_release);
    }

    void FVideoPipeline::EncodeLoop(const std::stop_token& stopToken)
    {
        uint64_t sentFrames = 0;
        while (!stopToken.stop_requested())
        {
            size_t slotIndex = 0;
            {
                std::unique_lock lock(encodeQueueMutex_);
                if (!encodeCv_.wait(lock, stopToken, [this]() { return !encodeQueue_.empty(); }))
                {
                    break;
                }
                slotIndex = encodeQueue_.front();
                encodeQueue_.pop_front();
            }

            FSlot& slot = *slots_[slotIndex];
            if (sinkCount_.load(std::memory_order_relaxed) == 0 || !slot.mapped)
            {
                slot.state.store(ESlotState::Free, std::memory_order_release);
                continue;
            }

            const uint32_t ySize = dstWidth_ * dstHeight_;
            FI420View view;
            view.y = slot.mapped;
            view.u = slot.mapped + ySize;
            view.v = slot.mapped + ySize + ySize / 4u;
            view.width = dstWidth_;
            view.height = dstHeight_;
            view.strideY = dstWidth_;
            view.strideC = dstWidth_ / 2u;
            const uint64_t timestampUs = slot.captureTimestampUs;

            if (keyframeRequested_.exchange(false, std::memory_order_relaxed))
            {
                encoder_.RequestKeyframe();
            }

            std::vector<std::byte> encodedFrame;
            bool keyframe = false;
            const bool encoded = encoder_.Encode(view, timestampUs / 1000u, encodedFrame, keyframe);
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
