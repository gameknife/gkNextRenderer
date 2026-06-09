#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Remote/VideoPipeline.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
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
        uint32_t MakeEven(uint32_t value, uint32_t fallback)
        {
            const uint32_t selected = value == 0 ? fallback : value;
            return std::max(2u, selected & ~1u);
        }

        std::chrono::steady_clock::duration FrameInterval(uint32_t fps)
        {
            return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(std::max(1u, fps))));
        }
    }

    FVideoPipeline::FVideoPipeline(RemoteServer::FConfig config)
        : config_(std::move(config))
        , frameSource_(MakeEven(config_.width, 640u), MakeEven(config_.height, 360u))
        , encoder_({MakeEven(config_.width, 640u), MakeEven(config_.height, 360u), std::max(1u, config_.fps),
                    std::max(1u, config_.bitrateKbps)})
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

        // Capture commands may still be in flight inside the renderer's frame command buffers.
        if (device_ && !slots_.empty())
        {
            device_->WaitIdle();
        }
        for (auto& slot : slots_)
        {
            DestroySlotResources(*slot);
        }
        slots_.clear();
        device_ = nullptr;
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

        FSlot* freeSlot = nullptr;
        for (auto& slot : slots_)
        {
            if (slot->state.load(std::memory_order_acquire) == ESlotState::Free)
            {
                freeSlot = slot.get();
                break;
            }
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

        const VkImage swapImage = swapChain.Images()[imageIndex];
        const VkExtent2D extent = swapChain.Extent();

        Vulkan::ImageMemoryBarrier::FullInsert(commandBuffer, swapImage, 0, VK_ACCESS_TRANSFER_READ_BIT,
                                               VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Vulkan::ImageMemoryBarrier::FullInsert(commandBuffer, freeSlot->image->Handle(), 0,
                                               VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.extent = {extent.width, extent.height, 1};
        vkCmdCopyImage(commandBuffer, swapImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, freeSlot->image->Handle(),
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

        // GENERAL keeps host reads of the linear image well-defined; visibility to the host is
        // guaranteed by the frame fence + HOST_COHERENT memory.
        Vulkan::ImageMemoryBarrier::FullInsert(commandBuffer, freeSlot->image->Handle(),
                                               VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                               VK_IMAGE_LAYOUT_GENERAL);
        Vulkan::ImageMemoryBarrier::FullInsert(commandBuffer, swapImage, VK_ACCESS_TRANSFER_READ_BIT, 0,
                                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        freeSlot->frameId = currentFrameId;
        freeSlot->captureTimestampUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - startedAt_).count());
        freeSlot->state.store(ESlotState::Recorded, std::memory_order_release);

        advanceThrottle();
    }

    bool FVideoPipeline::EnsureSlotResources(FSlot& slot, Vulkan::VulkanBaseRenderer& renderer)
    {
        const Vulkan::SwapChain& swapChain = renderer.SwapChain();
        const VkExtent2D extent = swapChain.Extent();
        const VkFormat format = swapChain.Format();
        if (extent.width == 0 || extent.height == 0)
        {
            return false;
        }
        if (slot.image && slot.extent.width == extent.width && slot.extent.height == extent.height &&
            slot.image->Format() == format)
        {
            return true;
        }

        DestroySlotResources(slot);
        device_ = &renderer.Device();

        slot.image = std::make_unique<Vulkan::Image>(*device_, extent, 1, format, VK_IMAGE_TILING_LINEAR,
                                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        // The CPU converter reads this memory every frame: prefer HOST_CACHED readback memory,
        // because reads from write-combined (coherent-only) memory are an order of magnitude slower.
        try
        {
            slot.memory = std::make_unique<Vulkan::DeviceMemory>(
                slot.image->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                           VK_MEMORY_PROPERTY_HOST_CACHED_BIT));
        }
        catch (const std::exception&)
        {
            slot.memory = std::make_unique<Vulkan::DeviceMemory>(
                slot.image->AllocateMemory(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        }
        device_->DebugUtils().SetObjectName(slot.image->Handle(), "RemoteVideo Capture Slot");

        VkImageSubresource subresource{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0};
        vkGetImageSubresourceLayout(device_->Handle(), slot.image->Handle(), &subresource, &slot.layout);

        slot.mapped = static_cast<uint8_t*>(slot.memory->Map(0, VK_WHOLE_SIZE));
        slot.extent = extent;

        switch (format)
        {
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            slot.swapRedBlue = false;
            break;
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            slot.swapRedBlue = true;
            break;
        default:
            slot.swapRedBlue = false;
            if (!warnedFormat_)
            {
                warnedFormat_ = true;
                SPDLOG_WARN("RemotePlay: unexpected swapchain format {}, assuming BGRA byte order",
                            static_cast<int>(format));
            }
            break;
        }
        return slot.mapped != nullptr;
    }

    void FVideoPipeline::DestroySlotResources(FSlot& slot)
    {
        if (slot.mapped && slot.memory)
        {
            slot.memory->Unmap();
        }
        slot.mapped = nullptr;
        slot.memory.reset();
        slot.image.reset();
        slot.extent = {0, 0};
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

            const FI420Frame& frame =
                frameSource_.ConvertBgra(slot.mapped + slot.layout.offset, static_cast<size_t>(slot.layout.rowPitch),
                                         slot.extent.width, slot.extent.height, slot.swapRedBlue);
            const uint64_t timestampUs = slot.captureTimestampUs;
            slot.state.store(ESlotState::Free, std::memory_order_release);

            if (keyframeRequested_.exchange(false, std::memory_order_relaxed))
            {
                encoder_.RequestKeyframe();
            }

            std::vector<std::byte> encodedFrame;
            bool keyframe = false;
            if (!encoder_.Encode(frame, timestampUs / 1000u, encodedFrame, keyframe))
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
