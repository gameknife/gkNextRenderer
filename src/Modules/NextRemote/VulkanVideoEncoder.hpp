#pragma once

#include "Modules/NextRemote/VideoEncoder.hpp"

#include "Modules/NextRemote/VulkanVideoCaps.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Vulkan
{
    class Device;
}

namespace Runtime::Remote
{
    enum class ERateControlMode : uint8_t;

    class FVulkanVideoEncoder final : public IVideoEncoder
    {
    public:
        FVulkanVideoEncoder(const Vulkan::Device& device, Vulkan::FVulkanVideoCaps caps, FVideoEncoderConfig config);
        ~FVulkanVideoEncoder();

        const char* Name() const override { return "vulkan"; }
        bool Start() override;
        void Stop() override;
        void RequestKeyframe() override;
        void SetBitrate(uint32_t bitrateKbps) override;
        bool SupportsGpuFrameInput() const override { return true; }
        bool Encode(const FI420View& view, uint64_t timestampMs, std::vector<std::byte>& outFrame, bool& keyframe) override;
        bool EncodeGpu(FGpuVideoFrame& frame, uint64_t timestampMs, std::vector<std::byte>& outFrame,
                       bool& keyframe) override;

    private:
        struct FAllocatedBuffer
        {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            uint8_t* mapped = nullptr;
            VkDeviceSize size = 0;
        };

        struct FAllocatedImage
        {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView view = VK_NULL_HANDLE;
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        };

        struct FDpbSlotState
        {
            bool valid = false;
            uint32_t frameNum = 0;
            int32_t picOrderCnt = 0;
            StdVideoH264PictureType pictureType = STD_VIDEO_H264_PICTURE_TYPE_INVALID;
        };

        struct FSessionMemoryBinding
        {
            VkDeviceMemory memory = VK_NULL_HANDLE;
            uint32_t bindIndex = 0;
            VkDeviceSize size = 0;
        };

        bool CreateVideoSession();
        bool CreateSessionParameters();
        bool CreateInputResources();
        bool CreateDpbResources();
        bool CreateBitstreamResources();
        bool CreateQueryPool();
        bool CreateCommandResources();
        bool LoadHeaders();
        ERateControlMode UpdateRateControlState();
        bool CopyI420ToNv12(const FI420View& view);
        bool RecordAndSubmitEncode(FGpuVideoFrame* gpuFrame, uint64_t timestampMs, std::vector<std::byte>& outFrame,
                                   bool& keyframe);

        uint32_t AlignUp(uint32_t value, uint32_t alignment) const;
        VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment) const;
        uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) const;
        bool AllocateMemory(const VkMemoryRequirements& requirements, VkMemoryPropertyFlags properties,
                            VkDeviceMemory& memory) const;
        void DestroyBuffer(FAllocatedBuffer& buffer);
        void DestroyImage(FAllocatedImage& image);
        void DestroySessionMemory();
        bool ResetAndBeginCommandBuffer(VkCommandBuffer& commandBuffer);
        bool EndSubmitAndWait(VkCommandBuffer commandBuffer);
        void FillSessionParameters();

        const Vulkan::Device& device_;
        Vulkan::FVulkanVideoCaps caps_;
        FVideoEncoderConfig config_;

        uint32_t codedWidth_ = 0;
        uint32_t codedHeight_ = 0;
        bool supportsInterFrames_ = false;
        bool started_ = false;
        bool forceKeyframe_ = true;
        bool rateControlDirty_ = true;
        bool firstControlCommand_ = true;
        uint32_t frameNum_ = 0;
        uint32_t pictureOrderCount_ = 0;
        uint16_t idrPicId_ = 0;

        VkVideoProfileInfoKHR profileInfo_{};
        VkVideoEncodeUsageInfoKHR usageInfo_{};
        VkVideoEncodeH264ProfileInfoKHR h264ProfileInfo_{};

        VkVideoSessionKHR session_ = VK_NULL_HANDLE;
        VkVideoSessionParametersKHR sessionParameters_ = VK_NULL_HANDLE;
        std::vector<FSessionMemoryBinding> sessionMemory_;

        FAllocatedImage inputImage_;
        static constexpr uint32_t dpbSlotCount = 2;
        std::array<FAllocatedImage, dpbSlotCount> dpbImages_{};
        std::array<FDpbSlotState, dpbSlotCount> dpbStates_{};
        int32_t lastReferenceSlotIndex_ = -1;
        FAllocatedBuffer stagingBuffer_;
        FAllocatedBuffer bitstreamBuffer_;

        VkQueryPool queryPool_ = VK_NULL_HANDLE;
        VkCommandPool commandPool_ = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
        VkFence encodeFence_ = VK_NULL_HANDLE;

        StdVideoH264SequenceParameterSet sps_{};
        StdVideoH264PictureParameterSet pps_{};
        std::vector<std::byte> parameterSetsAnnexb_;
    };
}
