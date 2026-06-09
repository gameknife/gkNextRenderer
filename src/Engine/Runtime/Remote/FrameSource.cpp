#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Remote/FrameSource.hpp"

#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

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

        VkSubresourceLayout GetScreenShotImageLayout(Vulkan::VulkanBaseRenderer& renderer)
        {
            VkSubresourceLayout layout{};
            const Vulkan::Image* image = renderer.GetScreenShotImage();
            if (!image)
            {
                return layout;
            }

            VkImageSubresource subresource{};
            subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            subresource.mipLevel = 0;
            subresource.arrayLayer = 0;
            vkGetImageSubresourceLayout(renderer.Device().Handle(), image->Handle(), &subresource, &layout);
            return layout;
        }

        std::array<uint8_t, 3> BgraToYuv(uint8_t b, uint8_t g, uint8_t r)
        {
            const int y = ((47 * r + 157 * g + 16 * b + 128) >> 8) + 16;
            const int u = ((-26 * r - 87 * g + 112 * b + 128) >> 8) + 128;
            const int v = ((112 * r - 102 * g - 10 * b + 128) >> 8) + 128;
            return {ClampByte(y), ClampByte(u), ClampByte(v)};
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

    bool FFrameSource::CaptureSwapChainFrame(Vulkan::VulkanBaseRenderer& renderer)
    {
        const Vulkan::SwapChain& swapChain = renderer.SwapChain();
        if (swapChain.IsHDR())
        {
            return false;
        }

        Vulkan::DeviceMemory* memory = renderer.GetScreenShotMemory();
        if (!memory || !renderer.GetScreenShotImage())
        {
            return false;
        }

        renderer.CaptureScreenShot();

        const VkExtent2D sourceExtent = swapChain.Extent();
        const VkSubresourceLayout layout = GetScreenShotImageLayout(renderer);
        if (sourceExtent.width == 0 || sourceExtent.height == 0 || layout.rowPitch == 0)
        {
            return false;
        }

        uint8_t* mappedData = static_cast<uint8_t*>(memory->Map(0, VK_WHOLE_SIZE));
        if (!mappedData)
        {
            return false;
        }

        const uint8_t* imageData = mappedData + layout.offset;
        const uint32_t width = frame_.width;
        const uint32_t height = frame_.height;
        const uint32_t chromaWidth = width / 2u;
        std::fill(frame_.u.begin(), frame_.u.end(), 0);
        std::fill(frame_.v.begin(), frame_.v.end(), 0);

        for (uint32_t y = 0; y < height; ++y)
        {
            const uint32_t srcY = std::min(sourceExtent.height - 1u,
                                           static_cast<uint32_t>((static_cast<uint64_t>(y) * sourceExtent.height) /
                                                                 std::max(1u, height)));
            for (uint32_t x = 0; x < width; ++x)
            {
                const uint32_t srcX = std::min(sourceExtent.width - 1u,
                                               static_cast<uint32_t>((static_cast<uint64_t>(x) * sourceExtent.width) /
                                                                     std::max(1u, width)));
                const uint8_t* pixel = imageData + static_cast<size_t>(srcY) * layout.rowPitch + srcX * 4u;
                const auto yuv = BgraToYuv(pixel[0], pixel[1], pixel[2]);
                frame_.y[static_cast<size_t>(y) * width + x] = yuv[0];

                const size_t chromaIndex = static_cast<size_t>(y / 2u) * chromaWidth + x / 2u;
                frame_.u[chromaIndex] = static_cast<uint8_t>(frame_.u[chromaIndex] + yuv[1] / 4u);
                frame_.v[chromaIndex] = static_cast<uint8_t>(frame_.v[chromaIndex] + yuv[2] / 4u);
            }
        }

        memory->Unmap();
        return true;
    }
}
