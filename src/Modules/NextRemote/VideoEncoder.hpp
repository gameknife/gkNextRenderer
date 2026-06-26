#pragma once

#include "Modules/NextRemote/FrameSource.hpp"

#include <fmt/format.h>
#include <vulkan/vulkan.h>
#include <vk_video/vulkan_video_codec_h264std.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Runtime::Remote
{
    constexpr uint32_t h264ProfileBaselineBit = 1u << 0;
    constexpr uint32_t h264ProfileMainBit = 1u << 1;
    constexpr uint32_t h264ProfileHighBit = 1u << 2;
    constexpr uint32_t h264ProfileAllBits = h264ProfileBaselineBit | h264ProfileMainBit | h264ProfileHighBit;

    enum class EVideoEncoderBackend : uint8_t
    {
        Auto,
        OpenH264,
        Vulkan,
    };

    inline constexpr const char* ToString(EVideoEncoderBackend backend)
    {
        switch (backend)
        {
        case EVideoEncoderBackend::Auto: return "auto";
        case EVideoEncoderBackend::OpenH264: return "openh264";
        case EVideoEncoderBackend::Vulkan: return "vulkan";
        default: return "unknown";
        }
    }

    struct FVideoEncoderConfig
    {
        uint32_t width = 640;
        uint32_t height = 360;
        uint32_t fps = 30;
        uint32_t bitrateKbps = 4000;
        int32_t h264ProfileIdc = -1;
    };

    struct FGpuVideoFrame
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout* layout = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    inline constexpr uint32_t H264ProfileMaskFromStdProfileIdc(int32_t profileIdc)
    {
        switch (profileIdc)
        {
        case STD_VIDEO_H264_PROFILE_IDC_BASELINE: return h264ProfileBaselineBit;
        case STD_VIDEO_H264_PROFILE_IDC_MAIN: return h264ProfileMainBit;
        case STD_VIDEO_H264_PROFILE_IDC_HIGH: return h264ProfileHighBit;
        default: return 0;
        }
    }

    inline constexpr const char* H264ProfileNameFromStdProfileIdc(int32_t profileIdc)
    {
        switch (profileIdc)
        {
        case STD_VIDEO_H264_PROFILE_IDC_BASELINE: return "ConstrainedBaseline";
        case STD_VIDEO_H264_PROFILE_IDC_MAIN: return "Main";
        case STD_VIDEO_H264_PROFILE_IDC_HIGH: return "High";
        default: return "Unknown";
        }
    }

    inline constexpr int32_t ChoosePreferredStdProfileIdc(uint32_t supportedMask)
    {
        if ((supportedMask & h264ProfileMainBit) != 0)
        {
            return STD_VIDEO_H264_PROFILE_IDC_MAIN;
        }
        if ((supportedMask & h264ProfileHighBit) != 0)
        {
            return STD_VIDEO_H264_PROFILE_IDC_HIGH;
        }
        if ((supportedMask & h264ProfileBaselineBit) != 0)
        {
            return STD_VIDEO_H264_PROFILE_IDC_BASELINE;
        }
        return STD_VIDEO_H264_PROFILE_IDC_BASELINE;
    }

    inline constexpr uint8_t H264LevelIdcForConfig(uint32_t width, uint32_t height, uint32_t fps)
    {
        if (width <= 1280 && height <= 720 && fps <= 60)
        {
            return 0x1f; // level 3.1
        }
        if (width <= 1920 && height <= 1080 && fps <= 60)
        {
            return 0x2a; // level 4.2
        }
        return 0x33; // level 5.1
    }

    inline std::string BuildH264FmtpLine(int32_t profileIdc, uint32_t width, uint32_t height, uint32_t fps)
    {
        uint8_t profileByte = 0x42;
        uint8_t constraintsByte = 0xE0;
        switch (profileIdc)
        {
        case STD_VIDEO_H264_PROFILE_IDC_MAIN:
            profileByte = 0x4D;
            constraintsByte = 0x00;
            break;
        case STD_VIDEO_H264_PROFILE_IDC_HIGH:
            profileByte = 0x64;
            constraintsByte = 0x00;
            break;
        case STD_VIDEO_H264_PROFILE_IDC_BASELINE:
        default:
            profileByte = 0x42;
            constraintsByte = 0xE0;
            break;
        }

        return fmt::format("profile-level-id={:02x}{:02x}{:02x};packetization-mode=1;level-asymmetry-allowed=1",
                           profileByte, constraintsByte, H264LevelIdcForConfig(width, height, fps));
    }

    class IVideoEncoder
    {
    public:
        virtual ~IVideoEncoder() = default;

        virtual const char* Name() const = 0;
        virtual bool Start() = 0;
        virtual void Stop() = 0;
        virtual void RequestKeyframe() = 0;
        virtual void SetBitrate(uint32_t bitrateKbps) = 0;
        virtual bool SupportsGpuFrameInput() const { return false; }
        virtual bool Encode(const FI420View& view, uint64_t timestampMs, std::vector<std::byte>& outFrame,
                            bool& keyframe) = 0;
        virtual bool EncodeGpu(FGpuVideoFrame& frame, uint64_t timestampMs, std::vector<std::byte>& outFrame,
                               bool& keyframe)
        {
            (void)frame;
            (void)timestampMs;
            outFrame.clear();
            keyframe = false;
            return false;
        }
    };
}
