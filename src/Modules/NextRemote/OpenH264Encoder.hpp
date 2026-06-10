#pragma once

#include "Modules/NextRemote/FrameSource.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

class ISVCEncoder;

namespace Runtime::Remote
{
    class FOpenH264Encoder final
    {
    public:
        struct FConfig
        {
            uint32_t width = 640;
            uint32_t height = 360;
            uint32_t fps = 30;
            uint32_t bitrateKbps = 4000;
        };

        explicit FOpenH264Encoder(FConfig config);
        ~FOpenH264Encoder();

        bool Start();
        void Stop();
        void RequestKeyframe();
        void SetBitrate(uint32_t bitrateKbps);

        bool Encode(const FI420Frame& frame, uint64_t timestampMs, std::vector<std::byte>& outFrame, bool& keyframe);
        bool Encode(const FI420View& view, uint64_t timestampMs, std::vector<std::byte>& outFrame, bool& keyframe);

    private:
        FConfig config_;
        ISVCEncoder* encoder_ = nullptr;
        bool forceKeyframe_ = true;
    };
}
