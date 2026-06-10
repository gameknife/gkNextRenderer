#pragma once

#include "Modules/NextRemote/FrameSource.hpp"
#include "Modules/NextRemote/OpenH264Encoder.hpp"
#include "Modules/NextRemote/RemoteServer.hpp"

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
#include <thread>
#include <vector>

namespace Vulkan
{
    class VulkanBaseRenderer;
    class Device;
    class Buffer;
    class DeviceMemory;

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

    // Single engine-wide video pipeline: converts the swapchain to I420 with a compute pass
    // recorded inside the in-flight frame command buffer (GPU scale + BT.709, zero CPU pixel
    // work), encodes on a dedicated worker thread and fans the bitstream out to every session.
    class FVideoPipeline final
    {
    public:
        using FPacketSink = std::function<void(const FEncodedPacket&)>;

        explicit FVideoPipeline(RemoteServer::FConfig config);
        ~FVideoPipeline();

        void Start();
        void Stop();

        // Render thread only: called while the frame command buffer is being recorded.
        void RecordFrame(VkCommandBuffer commandBuffer, uint32_t imageIndex, Vulkan::VulkanBaseRenderer& renderer);

        // Render thread, just before the renderer destroys the swapchain the convert pipeline
        // was created against.
        void ReleaseSwapChainResources();

        // Thread-safe; sinks are invoked on the encoder thread.
        uint64_t AddSink(FPacketSink sink);
        void RemoveSink(uint64_t sinkId);
        void RequestKeyframe();

    private:
        enum class ESlotState : int
        {
            Free,
            Recorded,
            Encoding
        };

        struct FSlot
        {
            std::unique_ptr<Vulkan::Buffer> buffer;
            std::unique_ptr<Vulkan::DeviceMemory> memory;
            uint8_t* mapped = nullptr;
            VkDeviceAddress address = 0;
            uint64_t frameId = 0;
            uint64_t captureTimestampUs = 0;
            std::atomic<ESlotState> state{ESlotState::Free};
        };

        bool EnsureSlotResources(FSlot& slot, Vulkan::VulkanBaseRenderer& renderer);
        void DestroySlotResources(FSlot& slot);
        void HarvestCompletedSlots(uint64_t currentFrameId);
        void EncodeLoop(const std::stop_token& stopToken);
        void Broadcast(const FEncodedPacket& packet);

        RemoteServer::FConfig config_;
        uint32_t dstWidth_ = 0;   // multiple of 8 (shader writes packed uints)
        uint32_t dstHeight_ = 0;  // multiple of 2

        // Capture slots (render thread records, encoder thread reads). The vector is filled once
        // on first RecordFrame and never resized afterwards.
        std::vector<std::unique_ptr<FSlot>> slots_;
        const Vulkan::Device* device_ = nullptr;
        std::unique_ptr<Vulkan::PipelineCommon::ZeroBindCustomPushConstantPipeline> convertPipeline_;

        std::chrono::steady_clock::time_point startedAt_;
        std::chrono::steady_clock::time_point nextFrameTime_;
        uint64_t droppedFrames_ = 0;
        bool warnedHdr_ = false;

        // Encoder worker
        std::mutex encodeQueueMutex_;
        std::condition_variable_any encodeCv_;
        std::deque<size_t> encodeQueue_;
        std::jthread encodeThread_;
        std::atomic_bool keyframeRequested_ = true;

        // Owned by the encoder thread after Start().
        FOpenH264Encoder encoder_;

        // Fan-out
        std::mutex sinksMutex_;
        std::map<uint64_t, FPacketSink> sinks_;
        uint64_t nextSinkId_ = 1;
        std::atomic<uint32_t> sinkCount_ = 0;
    };
}
