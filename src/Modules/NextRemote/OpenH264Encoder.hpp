#pragma once

#include "Modules/NextRemote/VideoEncoder.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class ISVCEncoder;

namespace Runtime::Remote
{
    class FOpenH264Encoder final : public IVideoEncoder
    {
    public:
        explicit FOpenH264Encoder(FVideoEncoderConfig config);
        ~FOpenH264Encoder();

        const char* Name() const override { return "openh264"; }
        bool Start() override;
        void Stop() override;
        void RequestKeyframe() override;
        void SetBitrate(uint32_t bitrateKbps) override;

        bool Encode(const FI420Frame& frame, uint64_t timestampMs, std::vector<std::byte>& outFrame, bool& keyframe);
        bool Encode(const FI420View& view, uint64_t timestampMs, std::vector<std::byte>& outFrame,
                    bool& keyframe) override;

    private:
        FVideoEncoderConfig config_;
        ISVCEncoder* encoder_ = nullptr;
        bool forceKeyframe_ = true;
    };
}
