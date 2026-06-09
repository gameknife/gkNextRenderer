#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Runtime::Remote
{
    struct FI420Frame
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> y;
        std::vector<uint8_t> u;
        std::vector<uint8_t> v;
    };

    // CPU-side BGRA → I420 conversion helper (scaling + BT.601-ish matrix). Pure CPU utility, no
    // Vulkan dependency; runs on the encoder worker thread.
    class FFrameSource final
    {
    public:
        FFrameSource(uint32_t width, uint32_t height);

        const FI420Frame& BuildTestPattern(uint64_t frameIndex);
        const FI420Frame& ConvertBgra(const uint8_t* data, size_t rowPitch, uint32_t srcWidth, uint32_t srcHeight,
                                      bool swapRedBlue);
        const FI420Frame& LatestFrame() const { return frame_; }

    private:
        FI420Frame frame_;
    };
}
