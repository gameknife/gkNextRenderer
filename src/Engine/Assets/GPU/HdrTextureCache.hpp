#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/GPU/Texture.hpp"

#include <utility>

namespace Vulkan
{
    class CommandPool;
}

namespace Assets
{
    class TextureImage;

    // Decoded HDR environment texture: base pixels + prefiltered mip chain +
    // projected SH, cached on disk (lzav-compressed) keyed by texture name.
    struct FHDRTexturePayload
    {
        int Width = 0;
        int Height = 0;
        uint32_t MipLevels = 0;
        VkFormat Format = VK_FORMAT_UNDEFINED;
        SphericalHarmonics SH {};
        std::vector<float> BasePixels;
        std::vector<std::vector<float>> MipLevelData;
        std::vector<std::pair<int, int>> MipDimensions;
    };

    // HDR residency promotion/demotion policy (frames), consumed by
    // GlobalTexturePool::TickHDRTextureResidency in Texture.cpp.
    inline constexpr uint32_t kHdrTexturePromotionFrames = 8;
    inline constexpr uint32_t kHdrTextureDemotionFrames = 180;

    const char* HDRResidencyName(GlobalTexturePool::EHDRTextureResidency residency);

    // Cooked cache file path for a named HDR texture
    std::string HDRCacheFileName(const std::string& textureName);

    // Cache-first load: disk cache, else stbi decode + prefilter + SH project,
    // then writes the cache. Empty payload (Width==0) on failure.
    FHDRTexturePayload LoadHDRTexturePayload(const std::string& textureName, const uint8_t* data, size_t byteLength);

    std::unique_ptr<TextureImage> CreateHDRTextureImage(
        Vulkan::CommandPool& commandPool, const FHDRTexturePayload& payload,
        GlobalTexturePool::EHDRTextureResidency residency);
}
