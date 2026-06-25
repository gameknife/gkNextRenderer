#pragma once

#include "Modules/NextRemote/FrameSource.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Runtime::Remote
{
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
    };

    class IVideoEncoder
    {
    public:
        virtual ~IVideoEncoder() = default;

        virtual const char* Name() const = 0;
        virtual bool Start() = 0;
        virtual void Stop() = 0;
        virtual void RequestKeyframe() = 0;
        virtual void SetBitrate(uint32_t bitrateKbps) = 0;
        virtual bool Encode(const FI420View& view, uint64_t timestampMs, std::vector<std::byte>& outFrame,
                            bool& keyframe) = 0;
    };
}
