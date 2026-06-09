#pragma once

#include <cstdint>
#include <vector>

namespace Vulkan
{
    class VulkanBaseRenderer;
}

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

    class FFrameSource final
    {
    public:
        FFrameSource(uint32_t width, uint32_t height);

        const FI420Frame& BuildTestPattern(uint64_t frameIndex);
        bool CaptureSwapChainFrame(Vulkan::VulkanBaseRenderer& renderer);
        const FI420Frame& LatestFrame() const { return frame_; }

    private:
        FI420Frame frame_;
    };
}
