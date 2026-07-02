#pragma once

#include "Modules/NextRemote/FrameSource.hpp"
#include "Modules/NextRemote/RemoteServer.hpp"
#include "Modules/NextRemote/VideoEncoder.hpp"
#include "Modules/NextRemote/VulkanVideoEncoder.hpp"

#include <vulkan/vulkan.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

namespace Vulkan
{
    class VulkanBaseRenderer;
    class Device;
    class Buffer;
    class DeviceMemory;
    class RenderImage;
    class TimelineSemaphore;

    namespace PipelineCommon
    {
        class ZeroBindCustomPushConstantPipeline;
    }
}

namespace Runtime::Remote
{
    struct FEncodedPacket
    {
        std::shared_ptr<const std::vector<std::byte>> annexb;
        bool keyframe = false;
        uint64_t timestampUs = 0;
    };

    // Single engine-wide video pipeline: converts the swapchain to NV12 with a compute pass
    // recorded inside the in-flight frame command buffer (GPU scale + BT.709), hands frames to
    // the Vulkan Video encoder on a dedicated worker thread, and fans the bitstream out to every
    // session.
    class FVideoPipeline final
    {
    public:
        using FPacketSink = std::function<void(const FEncodedPacket&)>;

        struct FStats
        {
            uint32_t sinkCount = 0;
            uint64_t droppedFrames = 0;
            uint32_t bitrateKbps = 0;
            uint32_t targetFps = 0;
            std::string_view activeEncoder;
        };

        explicit FVideoPipeline(RemoteServer::FConfig config);
        ~FVideoPipeline();

        bool Initialize(Vulkan::VulkanBaseRenderer& renderer);
        void Start();
        void Stop();

        // Render thread only: called while the frame command buffer is being recorded.
        void RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, Vulkan::VulkanBaseRenderer& renderer);
        void RecordFrameFromStorage(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                    Vulkan::VulkanBaseRenderer& renderer, uint32_t sourceBindlessIndex,
                                    VkExtent2D sourceExtent);

        // Render thread, just before the renderer destroys the swapchain the convert pipeline
        // was created against.
        void ReleaseSwapChainResources();

        // Thread-safe; sinks are invoked on the encoder thread.
        uint64_t AddSink(FPacketSink sink);
        void RemoveSink(uint64_t sinkId);
        void RequestKeyframe();
        void SetBitrate(uint32_t bitrateKbps);
        void SetTargetFps(uint32_t fps);
        void RegisterClientH264Profiles(std::string sessionId, uint32_t profileMask);
        void UnregisterClientH264Profiles(const std::string& sessionId);
        uint32_t BitrateKbps() const { return desiredBitrateKbps_.load(std::memory_order_relaxed); }
        uint32_t TargetFps() const { return targetFps_.load(std::memory_order_relaxed); }
        std::string_view ActiveEncoderName() const { return activeEncoderName_; }
        std::string_view RequestedEncoderName() const { return requestedEncoderName_; }
        FStats Stats() const;
        std::string OfferH264FmtpLine() const;

    private:
        enum class ESlotState : int
        {
            Free,
            Recorded,
            Encoding
        };

        struct FSlot
        {
            struct FExternalImage
            {
                VkImage image = VK_NULL_HANDLE;
                VkDeviceMemory memory = VK_NULL_HANDLE;
                VkImageView view = VK_NULL_HANDLE;
                VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            };

            std::unique_ptr<Vulkan::RenderImage> yPlane;
            std::unique_ptr<Vulkan::RenderImage> uvPlane;
            VkImageLayout yPlaneLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout uvPlaneLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            uint32_t yBindlessIndex = 0;
            uint32_t uvBindlessIndex = 0;
            FExternalImage encodeImage;
            uint64_t frameId = 0;
            uint64_t captureTimestampUs = 0;
            uint64_t completionValue = 0;
            std::atomic<ESlotState> state{ESlotState::Free};
        };

        bool EnsureSlotResources(FSlot& slot, Vulkan::VulkanBaseRenderer& renderer, size_t slotIndex);
        bool EnsureGpuSlotResources(FSlot& slot, Vulkan::VulkanBaseRenderer& renderer, size_t slotIndex);
        bool CreateGpuEncodeImage(FSlot& slot, const Vulkan::Device& device, size_t slotIndex);
        void RecordFrameFromSource(VkCommandBuffer commandBuffer, uint32_t imageIndex,
                                   Vulkan::VulkanBaseRenderer& renderer, uint32_t sourceBindlessIndex,
                                   VkExtent2D sourceExtent, VkImage swapChainImageForLegacyRestore);
        void DestroySlotResources(FSlot& slot);
        void DestroyGpuSlotResources(FSlot& slot);
        void HarvestCompletedSlots(uint64_t currentFrameId);
        FVideoEncoderConfig BuildEncoderConfig() const;
        int32_t ActiveH264ProfileIdcLocked() const;
        void RecomputeNegotiatedProfileLocked();
        void EncodeLoop(const std::stop_token& stopToken);
        void Broadcast(const FEncodedPacket& packet);

        RemoteServer::FConfig config_;
        uint32_t dstWidth_ = 0;   // multiple of 8 (shader writes packed uints)
        uint32_t dstHeight_ = 0;  // multiple of 2
        EVideoEncoderBackend requestedEncoder_ = EVideoEncoderBackend::Auto;
        const char* requestedEncoderName_ = "auto";
        const char* activeEncoderName_ = "vulkan";
        bool initialized_ = false;
        std::optional<Vulkan::FVulkanVideoCaps> vulkanCaps_;

        // Capture slots (render thread records, encoder thread reads). The vector is filled once
        // on first RecordFrame and never resized afterwards.
        std::vector<std::unique_ptr<FSlot>> slots_;
        const Vulkan::Device* device_ = nullptr;
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline> gpuConvertPipeline_;
        std::unique_ptr<Vulkan::TimelineSemaphore> graphicsCompletionSemaphore_;
        uint64_t nextCompletionValue_ = 1;
        bool useTimelineCompletion_ = false;

        std::chrono::steady_clock::time_point startedAt_;
        std::chrono::steady_clock::time_point nextFrameTime_;
        std::atomic<uint64_t> droppedFrames_{0};
        bool warnedHdr_ = false;

        // Encoder worker
        std::mutex encodeQueueMutex_;
        std::condition_variable_any encodeCv_;
        std::deque<size_t> encodeQueue_;
        std::jthread encodeThread_;
        std::atomic_bool keyframeRequested_ = true;
        std::atomic<uint32_t> desiredBitrateKbps_{4000};
        std::atomic<uint32_t> targetFps_{30};
        std::atomic_bool recreateEncoderRequested_ = false;

        // Owned by the encoder thread after Start().
        std::unique_ptr<IVideoEncoder> encoder_;

        mutable std::mutex profileMutex_;
        std::map<std::string, uint32_t> clientH264Profiles_;
        bool profileFrozen_ = false;
        int32_t negotiatedProfileIdc_ = STD_VIDEO_H264_PROFILE_IDC_BASELINE;

        // Fan-out
        std::mutex sinksMutex_;
        std::map<uint64_t, FPacketSink> sinks_;
        uint64_t nextSinkId_ = 1;
        std::atomic<uint32_t> sinkCount_ = 0;
    };
}
