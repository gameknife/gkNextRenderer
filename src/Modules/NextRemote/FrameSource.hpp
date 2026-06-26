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

    // Non-owning view over planar I420 data (e.g. a GPU-converted readback buffer).
    struct FI420View
    {
        const uint8_t* y = nullptr;
        const uint8_t* u = nullptr;
        const uint8_t* v = nullptr;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t strideY = 0;
        uint32_t strideC = 0;
    };

    // CPU-side I420 test pattern generator kept for test data generation; live remote capture
    // converts the swapchain to NV12 on the GPU before Vulkan Video encode.
    class FFrameSource final
    {
    public:
        FFrameSource(uint32_t width, uint32_t height);

        const FI420Frame& BuildTestPattern(uint64_t frameIndex);
        const FI420Frame& LatestFrame() const { return frame_; }

    private:
        FI420Frame frame_;
    };
}
