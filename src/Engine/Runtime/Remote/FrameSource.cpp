#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Remote/FrameSource.hpp"

#include <algorithm>
#include <array>

namespace Runtime::Remote
{
    namespace
    {
        uint8_t ClampByte(int value)
        {
            return static_cast<uint8_t>(std::clamp(value, 0, 255));
        }

        uint32_t MakeEven(uint32_t value)
        {
            return std::max(2u, value & ~1u);
        }
    }

    FFrameSource::FFrameSource(uint32_t width, uint32_t height)
    {
        frame_.width = MakeEven(width);
        frame_.height = MakeEven(height);
        frame_.y.resize(static_cast<size_t>(frame_.width) * frame_.height);
        frame_.u.resize(static_cast<size_t>(frame_.width / 2u) * (frame_.height / 2u));
        frame_.v.resize(frame_.u.size());
    }

    const FI420Frame& FFrameSource::BuildTestPattern(uint64_t frameIndex)
    {
        const uint32_t width = frame_.width;
        const uint32_t height = frame_.height;
        const int motion = static_cast<int>((frameIndex * 5u) % std::max(1u, width));

        for (uint32_t y = 0; y < height; ++y)
        {
            for (uint32_t x = 0; x < width; ++x)
            {
                const bool bar = ((x + static_cast<uint32_t>(motion)) / 80u) % 2u == 0u;
                const int gradient = static_cast<int>((x * 96u) / std::max(1u, width));
                const int scan = static_cast<int>((y * 48u) / std::max(1u, height));
                frame_.y[static_cast<size_t>(y) * width + x] = ClampByte((bar ? 86 : 150) + gradient + scan);
            }
        }

        const uint32_t chromaWidth = width / 2u;
        const uint32_t chromaHeight = height / 2u;
        for (uint32_t y = 0; y < chromaHeight; ++y)
        {
            for (uint32_t x = 0; x < chromaWidth; ++x)
            {
                const bool block = ((x + frameIndex / 3u) / 20u + y / 16u) % 2u == 0u;
                const size_t index = static_cast<size_t>(y) * chromaWidth + x;
                frame_.u[index] = block ? 88 : 170;
                frame_.v[index] = block ? 178 : 96;
            }
        }

        return frame_;
    }
}
