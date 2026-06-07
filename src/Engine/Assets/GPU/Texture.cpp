#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Utilities/StbImage.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Options.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "ThirdParty/lzav/lzav.h"

#include <spdlog/spdlog.h>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <system_error>
#include <webp/decode.h>

#if WITH_KTX2
#include <ktx.h>
#endif

#define M_NEXT_PI 3.14159265358979323846f

namespace
{
    constexpr uint32_t kHdrCacheMagic = 0x48445243; // 'HDRC'
    constexpr uint32_t kHdrCacheVersion = 1;
    constexpr uint32_t kHdrTexturePromotionFrames = 8;
    constexpr uint32_t kHdrTextureDemotionFrames = 180;

    struct HdrCacheHeader
    {
        uint32_t magic;
        uint32_t version;
        uint64_t originalSize;
        uint64_t compressedSize;
        uint64_t dataHash;
    };

    uint64_t HashBuffer(const uint8_t* data, size_t size)
    {
        constexpr uint64_t fnvOffset = 1469598103934665603ull;
        constexpr uint64_t fnvPrime = 1099511628211ull;

        uint64_t hash = fnvOffset;
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= static_cast<uint64_t>(data[i]);
            hash *= fnvPrime;
        }

        return hash;
    }

    bool ShouldEnableTextureWorkerUpload(const Vulkan::Device& device)
    {
        if (device.TransferFamilyIndex() == static_cast<int32_t>(device.GraphicsFamilyIndex()))
        {
            return false;
        }

        const bool validationEnabled = GOption && GOption->Validation;
        return !validationEnabled;
    }
}

namespace Assets
{
    struct TextureTaskContext
    {
        int32_t textureId;
        TextureImage* transferPtr;
        float elapsed;
        bool needFlushHDRSH;
        uint8_t hdrResidency;
        std::array<char, 256> outputInfo;
    };
    
    void PrefilterEnvironmentMapLevel(const float* sourcePixels, int sourceWidth, int sourceHeight,
                                    float* targetPixels, int targetWidth, int targetHeight, 
                                    float roughness)
    {
        const int sampleCount = std::max(1, static_cast<int>(8 * (1.0f - roughness) + 16 * roughness));
        
        for (int y = 0; y < targetHeight; ++y)
        {
            for (int x = 0; x < targetWidth; ++x)
            {
                // Convert target pixel to direction
                float u = (x + 0.5f) / targetWidth;
                float v = (y + 0.5f) / targetHeight;
                
                float theta = v * M_NEXT_PI;
                float phi = u * 2.0f * M_NEXT_PI;
                
                float sinTheta = std::sin(theta);
                float cosTheta = std::cos(theta);
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);
                
                // Main reflection direction
                float mainDirX = sinTheta * cosPhi;
                float mainDirY = cosTheta;
                float mainDirZ = sinTheta * sinPhi;
                
                // Build tangent space around main direction
                float upX = 0.0f, upY = 1.0f, upZ = 0.0f;
                if (std::abs(mainDirY) > 0.999f)
                {
                    upX = 1.0f; upY = 0.0f; upZ = 0.0f;
                }
                
                // Tangent vectors
                float tangentX = upY * mainDirZ - upZ * mainDirY;
                float tangentY = upZ * mainDirX - upX * mainDirZ;
                float tangentZ = upX * mainDirY - upY * mainDirX;
                
                float tangentLen = std::sqrt(tangentX * tangentX + tangentY * tangentY + tangentZ * tangentZ);
                tangentX /= tangentLen;
                tangentY /= tangentLen;
                tangentZ /= tangentLen;
                
                float bitangentX = mainDirY * tangentZ - mainDirZ * tangentY;
                float bitangentY = mainDirZ * tangentX - mainDirX * tangentZ;
                float bitangentZ = mainDirX * tangentY - mainDirY * tangentX;
                
                float colorR = 0.0f, colorG = 0.0f, colorB = 0.0f;
                float totalWeight = 0.0f;
                
                // Monte Carlo sampling
                for (int i = 0; i < sampleCount; ++i)
                {
                    // Generate random numbers (using simple pseudo-random for now)
                    float xi1 = static_cast<float>(i) / sampleCount;
                    float xi2 = static_cast<float>((i * 17 + 13) % sampleCount) / sampleCount;
                    
                    // Importance sampling for GGX distribution
                    float alpha = roughness * roughness;
                    float alpha2 = alpha * alpha;
                    
                    float cosTheta = std::sqrt((1.0f - xi1) / (1.0f + (alpha2 - 1.0f) * xi1));
                    float sinTheta = std::sqrt(1.0f - cosTheta * cosTheta);
                    float phi = 2.0f * M_NEXT_PI * xi2;
                    
                    // Local sample direction
                    float localX = sinTheta * std::cos(phi);
                    float localY = sinTheta * std::sin(phi);
                    float localZ = cosTheta;
                    
                    // Transform to world space
                    float worldX = localX * tangentX + localY * bitangentX + localZ * mainDirX;
                    float worldY = localX * tangentY + localY * bitangentY + localZ * mainDirY;
                    float worldZ = localX * tangentZ + localY * bitangentZ + localZ * mainDirZ;
                    
                    // Sample environment map
                    float sampleTheta = std::acos(std::clamp(worldY, -1.0f, 1.0f));
                    float samplePhi = std::atan2(worldZ, worldX);
                    if (samplePhi < 0) samplePhi += 2.0f * M_NEXT_PI;
                    
                    float sampleU = samplePhi / (2.0f * M_NEXT_PI);
                    float sampleV = sampleTheta / M_NEXT_PI;
                    
                    int sampleX = static_cast<int>(sampleU * sourceWidth) % sourceWidth;
                    int sampleY = static_cast<int>(sampleV * sourceHeight) % sourceHeight;
                    
                    int sampleIndex = (sampleY * sourceWidth + sampleX) * 4;
                    
                    float weight = 1.0f;
                    colorR += sourcePixels[sampleIndex + 0] * weight;
                    colorG += sourcePixels[sampleIndex + 1] * weight;
                    colorB += sourcePixels[sampleIndex + 2] * weight;
                    totalWeight += weight;
                }
                
                // Normalize and store result
                if (totalWeight > 0.0f)
                {
                    colorR /= totalWeight;
                    colorG /= totalWeight;
                    colorB /= totalWeight;
                }
                
                int targetIndex = (y * targetWidth + x) * 4;
                targetPixels[targetIndex + 0] = colorR;
                targetPixels[targetIndex + 1] = colorG;
                targetPixels[targetIndex + 2] = colorB;
                targetPixels[targetIndex + 3] = 1.0f;
            }
        }
    }

    void PrefilterHdrEnvironmentMap(const float* hdrPixels, int width, int height, 
                             std::vector<std::vector<float>>& mipLevels,
                             std::vector<std::pair<int, int>>& mipDimensions)
    {
        constexpr int maxMipLevels = 8; // Typically 5-8 levels for environment maps
        mipLevels.clear();
        mipDimensions.clear();
        
        // Calculate mip levels
        int currentWidth = width;
        int currentHeight = height;
        
        for (int mipLevel = 0; mipLevel < maxMipLevels; ++mipLevel)
        {
            if (currentWidth < 4 || currentHeight < 4) break;
            
            mipDimensions.push_back({currentWidth, currentHeight});
            mipLevels.emplace_back(currentWidth * currentHeight * 4); // RGBA

            if (mipLevel > 0)
            {
                float roughness = static_cast<float>(mipLevel) / (maxMipLevels - 1);
                PrefilterEnvironmentMapLevel(hdrPixels, width, height, 
                                           mipLevels[mipLevel].data(), 
                                           currentWidth, currentHeight, roughness);
            }
            
            currentWidth = std::max(1, currentWidth / 2);
            currentHeight = std::max(1, currentHeight / 2);
        }
    }

    SphericalHarmonics ProjectHdrToSh(const float* hdrPixels, int width, int height)
    {
        SphericalHarmonics result{};
        
        // Initialize coefficients to zero
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 9; ++j)
                result.coefficients[i][j] = 0.0f;
        
        // SH basis function evaluation constants
        constexpr float shC0 = 0.282095f; // 1/(2*sqrt(π))
        constexpr float shC1 = 0.488603f; // sqrt(3)/(2*sqrt(π))
        constexpr float shC2 = 1.092548f; // sqrt(15)/(2*sqrt(π))
        constexpr float shC3 = 0.315392f; // sqrt(5)/(4*sqrt(π))
        constexpr float shC4 = 0.546274f; // sqrt(15)/(4*sqrt(π))
        
        float weightSum = 0.0f;
        
        // For each pixel in the environment map
        for (int y = 0; y < height; ++y)
        {
            // Calculate spherical coordinates
            float v = (y + 0.5f) / height;
            float theta = v * M_NEXT_PI;
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);
            
            // Pixel solid angle weight (important for correct integration)
            float weight = sinTheta * (M_NEXT_PI / height) * (2.0f * M_NEXT_PI / width);
            
            for (int x = 0; x < width; ++x)
            {
                float u = (x + 0.5f) / width;
                float phi = u * 2.0f * M_NEXT_PI;
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);
                
                // Convert to direction vector
                float dx = sinTheta * cosPhi;
                float dy = cosTheta;
                float dz = sinTheta * sinPhi;
                
                // Evaluate SH basis functions
                float basis[9];
                // Band 0 (1 coefficient)
                basis[0] = shC0;
                
                // Band 1 (3 coefficients)
                basis[1] = -shC1 * dy;
                basis[2] = shC1 * dz;
                basis[3] = -shC1 * dx;
                
                // Band 2 (5 coefficients)
                basis[4] = shC2 * dx * dy;
                basis[5] = -shC2 * dy * dz;
                basis[6] = shC3 * (3.0f * dy * dy - 1.0f);
                basis[7] = -shC2 * dx * dz;
                basis[8] = shC4 * (dx * dx - dz * dz);
                
                // Get pixel color (RGBA format, we want RGB)
                int pixelIndex = (y * width + x) * 4;
                float r = hdrPixels[pixelIndex + 0];
                float g = hdrPixels[pixelIndex + 1];
                float b = hdrPixels[pixelIndex + 2];
                
                // Project color onto SH basis functions
                for (int i = 0; i < 9; ++i)
                {
                    result.coefficients[0][i] += r * basis[i] * weight;
                    result.coefficients[1][i] += g * basis[i] * weight;
                    result.coefficients[2][i] += b * basis[i] * weight;
                }
                
                weightSum += weight;
            }
        }
                
        return result;
    }

    struct FHDRTexturePayload
    {
        int Width = 1;
        int Height = 1;
        uint32_t MipLevels = 1;
        VkFormat Format = VK_FORMAT_R32G32B32A32_SFLOAT;
        std::vector<float> BasePixels = {0.0f, 0.0f, 0.0f, 1.0f};
        std::vector<std::vector<float>> MipLevelData;
        std::vector<std::pair<int, int>> MipDimensions;
        SphericalHarmonics SH {};
    };

    const char* HDRResidencyName(GlobalTexturePool::EHDRTextureResidency residency)
    {
        return residency == GlobalTexturePool::EHDRTextureResidency::FullMip ? "full" : "lowest-mip";
    }

    std::string HDRCacheFileName(const std::string& textureName)
    {
        std::hash<std::string> hasher;
        return Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", hasher(textureName)), "texhdr");
    }

    bool LoadHDRTexturePayloadFromCache(const std::string& textureName, FHDRTexturePayload& payload)
    {
        const std::string cacheFileName = HDRCacheFileName(textureName);
        std::filesystem::path cacheFilePath(cacheFileName);
        if (!std::filesystem::exists(cacheFilePath))
        {
            return false;
        }

        bool validCache = false;
        std::ifstream cacheFile(cacheFileName, std::ios::binary);
        if (cacheFile.is_open())
        {
            HdrCacheHeader header {};
            cacheFile.read(reinterpret_cast<char*>(&header), sizeof(header));

            validCache = cacheFile.gcount() == sizeof(header)
                && header.magic == kHdrCacheMagic
                && header.version == kHdrCacheVersion
                && header.originalSize > 0
                && header.compressedSize > 0
                && header.originalSize <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())
                && header.compressedSize <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())
                && header.originalSize <= static_cast<uint64_t>(std::numeric_limits<int>::max())
                && header.compressedSize <= static_cast<uint64_t>(std::numeric_limits<int>::max());

            if (validCache)
            {
                const size_t compressedSize = static_cast<size_t>(header.compressedSize);
                const size_t originalSize = static_cast<size_t>(header.originalSize);

                std::vector<uint8_t> compressedData(compressedSize);
                cacheFile.read(reinterpret_cast<char*>(compressedData.data()), compressedSize);
                if (!cacheFile)
                {
                    validCache = false;
                }
                else
                {
                    std::vector<uint8_t> uncompressedData(originalSize);
                    const size_t decompressedSize = lzav_decompress(
                        compressedData.data(), uncompressedData.data(),
                        static_cast<int>(compressedSize), static_cast<int>(originalSize));

                    if (decompressedSize != originalSize
                        || HashBuffer(uncompressedData.data(), uncompressedData.size()) != header.dataHash)
                    {
                        validCache = false;
                    }
                    else
                    {
                        size_t offset = 0;
                        auto readFromBuffer = [&](void* dst, size_t readSize) -> bool
                        {
                            if (offset + readSize > uncompressedData.size())
                            {
                                return false;
                            }
                            std::memcpy(dst, uncompressedData.data() + offset, readSize);
                            offset += readSize;
                            return true;
                        };

                        size_t mipCount = 0;
                        if (!readFromBuffer(&payload.Width, sizeof(int))
                            || !readFromBuffer(&payload.Height, sizeof(int))
                            || !readFromBuffer(&payload.MipLevels, sizeof(uint32_t))
                            || !readFromBuffer(&payload.SH, sizeof(SphericalHarmonics))
                            || !readFromBuffer(&mipCount, sizeof(size_t))
                            || payload.Width <= 0
                            || payload.Height <= 0
                            || mipCount == 0)
                        {
                            validCache = false;
                        }
                        else
                        {
                            payload.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
                            payload.MipDimensions.resize(mipCount);
                            for (auto& dim : payload.MipDimensions)
                            {
                                if (!readFromBuffer(&dim.first, sizeof(int))
                                    || !readFromBuffer(&dim.second, sizeof(int))
                                    || dim.first <= 0
                                    || dim.second <= 0)
                                {
                                    validCache = false;
                                    break;
                                }
                            }
                        }

                        if (validCache)
                        {
                            const size_t baseFloats = static_cast<size_t>(payload.Width) * payload.Height * 4;
                            payload.BasePixels.resize(baseFloats);
                            if (!readFromBuffer(payload.BasePixels.data(), baseFloats * sizeof(float)))
                            {
                                validCache = false;
                            }
                        }

                        if (validCache)
                        {
                            payload.MipLevelData.clear();
                            payload.MipLevelData.resize(mipCount);
                            for (auto& mipData : payload.MipLevelData)
                            {
                                size_t mipSize = 0;
                                if (!readFromBuffer(&mipSize, sizeof(size_t)))
                                {
                                    validCache = false;
                                    break;
                                }
                                mipData.resize(mipSize);
                                if (mipSize > 0 && !readFromBuffer(mipData.data(), mipSize * sizeof(float)))
                                {
                                    validCache = false;
                                    break;
                                }
                            }
                        }
                    }
                }
            }

            cacheFile.close();
        }

        if (!validCache)
        {
            std::error_code removeError;
            std::filesystem::remove(cacheFilePath, removeError);
        }
        return validCache;
    }

    void SaveHDRTexturePayloadToCache(const std::string& textureName, const FHDRTexturePayload& payload)
    {
        std::vector<uint8_t> uncompressedData;
        auto writeToBuffer = [&](const void* src, size_t writeSize)
        {
            const uint8_t* bytes = static_cast<const uint8_t*>(src);
            uncompressedData.insert(uncompressedData.end(), bytes, bytes + writeSize);
        };

        writeToBuffer(&payload.Width, sizeof(int));
        writeToBuffer(&payload.Height, sizeof(int));
        writeToBuffer(&payload.MipLevels, sizeof(uint32_t));
        writeToBuffer(&payload.SH, sizeof(SphericalHarmonics));

        const size_t mipCount = payload.MipDimensions.size();
        writeToBuffer(&mipCount, sizeof(size_t));
        for (const auto& dim : payload.MipDimensions)
        {
            writeToBuffer(&dim.first, sizeof(int));
            writeToBuffer(&dim.second, sizeof(int));
        }

        writeToBuffer(payload.BasePixels.data(), payload.BasePixels.size() * sizeof(float));

        for (const auto& mipData : payload.MipLevelData)
        {
            const size_t mipSize = mipData.size();
            writeToBuffer(&mipSize, sizeof(size_t));
            writeToBuffer(mipData.data(), mipSize * sizeof(float));
        }

        const size_t uncompressedSize = uncompressedData.size();
        if (uncompressedSize > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            return;
        }

        const size_t compressedBound = lzav_compress_bound_hi(int(uncompressedSize));
        std::vector<uint8_t> compressedData(compressedBound);
        const size_t actualCompressedSize = lzav_compress_hi(
            uncompressedData.data(), compressedData.data(), int(uncompressedSize), int(compressedBound));
        if (actualCompressedSize == 0)
        {
            return;
        }

        HdrCacheHeader header {};
        header.magic = kHdrCacheMagic;
        header.version = kHdrCacheVersion;
        header.originalSize = static_cast<uint64_t>(uncompressedSize);
        header.compressedSize = static_cast<uint64_t>(actualCompressedSize);
        header.dataHash = HashBuffer(uncompressedData.data(), uncompressedData.size());

        const std::string cacheFileName = HDRCacheFileName(textureName);
        std::filesystem::path cacheFilePath(cacheFileName);
        std::filesystem::path tempCachePath = cacheFilePath;
        tempCachePath += ".tmp";

        std::ofstream cacheFile(tempCachePath, std::ios::binary | std::ios::trunc);
        if (!cacheFile.is_open())
        {
            std::error_code removeTempError;
            std::filesystem::remove(tempCachePath, removeTempError);
            return;
        }

        cacheFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
        cacheFile.write(reinterpret_cast<const char*>(compressedData.data()), actualCompressedSize);
        cacheFile.flush();
        cacheFile.close();

        std::error_code removeError;
        std::filesystem::remove(cacheFilePath, removeError);

        std::error_code renameError;
        std::filesystem::rename(tempCachePath, cacheFilePath, renameError);
        if (renameError)
        {
            std::filesystem::remove(tempCachePath);
        }
    }

    FHDRTexturePayload LoadHDRTexturePayload(const std::string& textureName, const uint8_t* data, size_t byteLength)
    {
        FHDRTexturePayload payload {};
        if (LoadHDRTexturePayloadFromCache(textureName, payload))
        {
            return payload;
        }

        if (data == nullptr || byteLength == 0)
        {
            return payload;
        }

        int channels = 4;
        float* pixels = stbi_loadf_from_memory(data, static_cast<uint32_t>(byteLength), &payload.Width, &payload.Height,
                                               &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            return payload;
        }

        payload.Format = VK_FORMAT_R32G32B32A32_SFLOAT;
        const size_t baseFloats = static_cast<size_t>(payload.Width) * payload.Height * 4;
        payload.BasePixels.assign(pixels, pixels + baseFloats);
        payload.SH = ProjectHdrToSh(payload.BasePixels.data(), payload.Width, payload.Height);
        PrefilterHdrEnvironmentMap(payload.BasePixels.data(), payload.Width, payload.Height, payload.MipLevelData,
                                   payload.MipDimensions);
        payload.MipLevels = static_cast<uint32_t>(payload.MipLevelData.size());
        stbi_image_free(pixels);

        SaveHDRTexturePayloadToCache(textureName, payload);
        return payload;
    }

    std::unique_ptr<TextureImage> CreateHDRTextureImage(
        Vulkan::CommandPool& commandPool, const FHDRTexturePayload& payload,
        GlobalTexturePool::EHDRTextureResidency residency)
    {
        const uint32_t baseSize = static_cast<uint32_t>(payload.BasePixels.size() * sizeof(float));
        if (residency == GlobalTexturePool::EHDRTextureResidency::FullMip
            && payload.MipLevelData.size() > 1
            && payload.MipDimensions.size() == payload.MipLevelData.size())
        {
            return std::make_unique<TextureImage>(
                commandPool, payload.Width, payload.Height, static_cast<uint32_t>(payload.MipLevelData.size()),
                payload.Format, reinterpret_cast<const unsigned char*>(payload.BasePixels.data()), baseSize,
                payload.MipLevelData, payload.MipDimensions);
        }

        if (residency == GlobalTexturePool::EHDRTextureResidency::LowestMip
            && payload.MipLevelData.size() > 1
            && payload.MipDimensions.size() == payload.MipLevelData.size()
            && !payload.MipLevelData.back().empty())
        {
            const auto& dim = payload.MipDimensions.back();
            const auto& mipData = payload.MipLevelData.back();
            const uint32_t mipSize = static_cast<uint32_t>(mipData.size() * sizeof(float));
            return std::make_unique<TextureImage>(
                commandPool, dim.first, dim.second, 1, payload.Format,
                reinterpret_cast<const unsigned char*>(mipData.data()), mipSize);
        }

        return std::make_unique<TextureImage>(
            commandPool, payload.Width, payload.Height, 1, payload.Format,
            reinterpret_cast<const unsigned char*>(payload.BasePixels.data()), baseSize);
    }

    uint32_t GlobalTexturePool::LoadTexture(const std::string& filename, bool srgb)
    {
        auto& pakSystem = Utilities::Package::FPackageFileSystem::GetInstance();
        const bool hasMountedEntry = pakSystem.HasMountedEntry(filename);
        const std::string absPath = Utilities::FileHelper::GetPlatformFilePath(filename.c_str());
        std::error_code existsError;
        const bool hasOsFile = std::filesystem::exists(absPath, existsError);

        if (!hasMountedEntry && !hasOsFile)
        {
            SPDLOG_WARN("Texture '{}' is unavailable; using a placeholder texture.", filename);
            return GetInstance()->RequestNewTextureMemAsync(
                filename, "image/png", false, nullptr, 0, srgb, ETextureLifetime::ETL_Transient);
        }

        std::vector<uint8_t> data;
        pakSystem.LoadFile(filename, data);
        std::filesystem::path path(filename);
        std::string mime = std::string("image/") + path.extension().string().substr(1);
        return GetInstance()->RequestNewTextureMemAsync(
            filename, mime, false, data.data(), data.size(), srgb, ETextureLifetime::ETL_Transient);
    }

    uint32_t GlobalTexturePool::LoadTexture(const std::string& texname, const std::string& mime,
                                            const unsigned char* data, size_t bytelength, bool srgb)
    {
        return GetInstance()->RequestNewTextureMemAsync(
            texname, mime, false, data, bytelength, srgb, ETextureLifetime::ETL_Transient);
    }

    uint32_t GlobalTexturePool::LoadHDRTexture(const std::string& filename)
    {
        auto& pakSystem = Utilities::Package::FPackageFileSystem::GetInstance();
        const bool hasMountedEntry = pakSystem.HasMountedEntry(filename);
        const std::string absPath = Utilities::FileHelper::GetPlatformFilePath(filename.c_str());
        std::error_code existsError;
        const bool hasOsFile = std::filesystem::exists(absPath, existsError);

        if (!hasMountedEntry && !hasOsFile)
        {
            SPDLOG_WARN("HDR texture '{}' is unavailable; using a placeholder environment.", filename);
            return GetInstance()->RequestNewTextureMemAsync(
                filename, "image/hdr", true, nullptr, 0, false, ETextureLifetime::ETL_Persistent);
        }

        std::vector<uint8_t> data;
        const bool loaded = pakSystem.LoadFile(filename, data);
        if (!loaded || data.empty())
        {
            SPDLOG_WARN("HDR texture '{}' is unavailable; using a placeholder environment.", filename);
            return GetInstance()->RequestNewTextureMemAsync(
                filename, "image/hdr", true, nullptr, 0, false, ETextureLifetime::ETL_Persistent);
        }

        return GetInstance()->RequestNewTextureMemAsync(
            filename, "image/hdr", true, data.data(), data.size(), false, ETextureLifetime::ETL_Persistent);
    }

    TextureImage* GlobalTexturePool::GetTextureImage(uint32_t idx)
    {
        if (GetInstance()->textureImages_.size() > idx)
        {
            return GetInstance()->textureImages_[idx].get();
        }
        return nullptr;
    }

    TextureImage* GlobalTexturePool::GetTextureImageByName(const std::string& name)
    {
        uint32_t id = GetTextureIndexByName(name);
        if (id != -1)
        {
            return GetInstance()->textureImages_[id].get();
        }
        return nullptr;
    }

    uint32_t GlobalTexturePool::GetTextureIndexByName(const std::string& name)
    {
        if (GetInstance()->textureNameMap_.find(name) != GetInstance()->textureNameMap_.end())
        {
            return GetInstance()->textureNameMap_[name].GlobalIdx_;
        }
        return -1;
    }

    GlobalTexturePool::GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& commandPool,
                                         Vulkan::CommandPool& commandPoolMt) :
        device_(device),
        commandPool_(commandPool),
        mainThreadCommandPool_(commandPoolMt),
        textureWorkerUploadEnabled_(ShouldEnableTextureWorkerUpload(device))
    {
        if (!textureWorkerUploadEnabled_)
        {
            SPDLOG_INFO("Texture uploads will run on the main thread because no dedicated transfer queue is available or validation mode is active");
        }

        static const uint32_t kMaxBindlessResources = 65535u;// moltenVK returns a invalid value. std::min(65535u, device.DeviceProperties().limits.maxPerStageDescriptorSamplers);
        static const uint32_t kMaxBindlessShadowMaps = 16u;
        // Last binding must have the most descriptors because DescriptorSetLayout
        // puts VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT on the last binding,
        // and DescriptorSets allocates with variableDescriptorCount = 65534.
        const std::vector<Vulkan::DescriptorBinding> descriptorBindings =
        {
            {2, kMaxBindlessShadowMaps, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_ALL},
            {0, kMaxBindlessResources, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_ALL},
            {1, kMaxBindlessResources, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_ALL},
        };
        descriptorSetManager_.reset(new Vulkan::DescriptorSetManager(device, descriptorBindings, 1, true));

        // for hdr to bind
        hdrSphericalHarmonics_.resize(100);

        GlobalTexturePool::instance_ = this;

        CreateDefaultTextures();
    }

    GlobalTexturePool::~GlobalTexturePool()
    {
        defaultWhiteTexture_.reset();
        textureImages_.clear();
        descriptorSetManager_.reset();
    }

    void GlobalTexturePool::BindTexture(uint32_t textureIdx, const TextureImage& textureImage)
    {
        auto& descriptorSets = descriptorSetManager_->DescriptorSets();
        const VkDescriptorImageInfo imageInfo{
            textureImage.Sampler().Handle(),
            textureImage.ImageView().Handle(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        std::vector<VkWriteDescriptorSet> descriptorWrites =
        {
            descriptorSets.Bind(0, 0, imageInfo, textureIdx, 1),
        };
        descriptorSets.UpdateDescriptors(0, descriptorWrites);
    }

    void GlobalTexturePool::BindStorageTexture(uint32_t textureIdx, const Vulkan::ImageView& textureImage)
    {
        auto& descriptorSets = descriptorSetManager_->DescriptorSets();
        const VkDescriptorImageInfo imageInfo{
            VK_NULL_HANDLE,
            textureImage.Handle(),
            VK_IMAGE_LAYOUT_GENERAL,
        };
        std::vector<VkWriteDescriptorSet> descriptorWrites =
        {
            descriptorSets.Bind(0, 1, imageInfo, textureIdx, 1),
        };
        descriptorSets.UpdateDescriptors(0, descriptorWrites);
    }

    void GlobalTexturePool::BindShadowMap(uint32_t slot, const Vulkan::ImageView& view, const Vulkan::Sampler& sampler)
    {
        auto& descriptorSets = descriptorSetManager_->DescriptorSets();
        const VkDescriptorImageInfo imageInfo{
            sampler.Handle(),
            view.Handle(),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        };
        std::vector<VkWriteDescriptorSet> descriptorWrites =
        {
            descriptorSets.Bind(0, 2, imageInfo, slot, 1),
        };
        descriptorSets.UpdateDescriptors(0, descriptorWrites);
    }

    uint32_t GlobalTexturePool::RegisterTexture(const std::string& textureName, std::unique_ptr<TextureImage> textureImage,
                                                ETextureLifetime lifetime)
    {
        if (!textureImage)
        {
            return static_cast<uint32_t>(-1);
        }

        textureImage->SetDebugName(fmt::format("Texture {}", textureName));

        uint32_t textureIdx = 0;
        auto textureIt = textureNameMap_.find(textureName);
        if (textureIt != textureNameMap_.end())
        {
            textureIdx = textureIt->second.GlobalIdx_;
            textureIt->second.Status_ = ETextureStatus::ETS_Loaded;
            textureIt->second.Lifetime_ = lifetime;
            if (textureImages_.size() <= textureIdx)
            {
                textureImages_.resize(static_cast<size_t>(textureIdx) + 1);
            }
            textureImages_[textureIdx] = std::move(textureImage);
        }
        else
        {
            textureIdx = static_cast<uint32_t>(textureImages_.size());
            textureNameMap_[textureName] = {textureIdx, ETextureStatus::ETS_Loaded, lifetime};
            textureImages_.push_back(std::move(textureImage));
        }

        BindTexture(textureIdx, *textureImages_[textureIdx]);
        return textureIdx;
    }

    void GlobalTexturePool::ReleaseTexture(uint32_t textureIdx)
    {
        if (textureIdx >= textureImages_.size() || !textureImages_[textureIdx])
        {
            return;
        }

        // Descriptor sets may still be referenced by in-flight UI command buffers.
        device_.WaitIdle();

        textureImages_[textureIdx].reset();
        if (defaultWhiteTexture_)
        {
            BindTexture(textureIdx, *defaultWhiteTexture_);
        }

        std::erase_if(textureNameMap_, [textureIdx](const auto& item)
        {
            return item.second.GlobalIdx_ == textureIdx;
        });
    }

    uint32_t GlobalTexturePool::TryGetTexureIndex(const std::string& textureName) const
    {
        if (textureNameMap_.find(textureName) != textureNameMap_.end())
        {
            return textureNameMap_.at(textureName).GlobalIdx_;
        }
        return -1;
    }

    uint32_t GlobalTexturePool::RequestNewTextureMemAsync(const std::string& texname, const std::string& mime, bool hdr,
                                                          const unsigned char* data, size_t bytelength, bool srgb,
                                                          ETextureLifetime lifetime)
    {
        uint32_t newTextureIdx = 0;
        if (textureNameMap_.find(texname) != textureNameMap_.end())
        {
            // 这里要判断一下，如果TextureUnLoaded，重新绑定
            if(textureNameMap_[texname].Status_ == ETextureStatus::ETS_Unloaded)
            {
                textureNameMap_[texname].Status_ = ETextureStatus::ETS_Loaded;
                textureNameMap_[texname].Lifetime_ = lifetime;
                newTextureIdx = textureNameMap_[texname].GlobalIdx_;
            }
            else
            {
                textureNameMap_[texname].Lifetime_ = lifetime;
                // 这里要判断一下，如果已经加载了，直接返回
                return textureNameMap_[texname].GlobalIdx_;
            }
        }
        else
        {
            textureImages_.emplace_back(nullptr);
            newTextureIdx = static_cast<uint32_t>(textureImages_.size()) - 1;
            textureNameMap_[texname] = { newTextureIdx, ETextureStatus::ETS_Loaded, lifetime };
        }

        // load parse bind texture into newTextureIdx with transfer queue

        uint8_t* copyedData = nullptr;
        if (bytelength > 0)
        {
            copyedData = new uint8_t[bytelength];
            memcpy(copyedData, data, bytelength);
        }
        const bool streamHDRAtLoad = hdr && NextEngine::GetInstance() != nullptr
            && NextEngine::GetInstance()->GetUserSettings().StreamHDRTextures;
        const EHDRTextureResidency initialHDRResidency =
            streamHDRAtLoad ? EHDRTextureResidency::LowestMip : EHDRTextureResidency::FullMip;
        if (hdr)
        {
            if (hdrTextureResidency_.size() <= newTextureIdx)
            {
                hdrTextureResidency_.resize(static_cast<size_t>(newTextureIdx) + 1);
            }
            auto& residency = hdrTextureResidency_[newTextureIdx];
            residency.TextureName = texname;
            residency.Lifetime = lifetime;
            residency.IsHDR = true;
            residency.Current = initialHDRResidency;
            residency.Target = initialHDRResidency;
            residency.Pending = true;
            residency.DemandFrames = 0;
            residency.LastTouchedFrame = 0;
        }
        auto textureLoadTask =
            [this, hdr, srgb, texname, mime, copyedData, bytelength, newTextureIdx, initialHDRResidency](Tasks::ResTask& task)
            {
                TextureTaskContext taskContext{};
                const auto timer = std::chrono::high_resolution_clock::now();

                // Load the texture in normal host memory.
                int width = 32;
                int height = 32;
                int channels = 4;
                uint8_t* stbdata = nullptr;
                uint8_t* pixels = nullptr;
                uint32_t size = 0;
                uint32_t miplevel = 1;
                VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
                bool textureCreated = false;
#if WITH_KTX2
                ktxTexture2* kTexture = nullptr;
                ktx_error_code_e result;
#endif
                bool useWebPFree = false;

                auto createHdrPlaceholderTexture = [&]()
                {
                    static constexpr std::array<float, 4> kPlaceholderHdrPixel = {0.0f, 0.0f, 0.0f, 1.0f};

                    width = 1;
                    height = 1;
                    channels = 4;
                    size = static_cast<uint32_t>(kPlaceholderHdrPixel.size() * sizeof(float));
                    miplevel = 1;
                    format = VK_FORMAT_R32G32B32A32_SFLOAT;

                    hdrSphericalHarmonics_[newTextureIdx] = {};
                    textureImages_[newTextureIdx] = std::make_unique<TextureImage>(
                        commandPool_, width, height, miplevel, format,
                        reinterpret_cast<const unsigned char*>(kPlaceholderHdrPixel.data()), size);
                    textureCreated = true;
                };

                // load from ktx inside glb
                if (mime.find("image/ktx") != std::string::npos)
                {
#if WITH_KTX2
                    auto loadKtxFromMemory = [&]() -> bool {
                        result = ktxTexture2_CreateFromMemory(copyedData, bytelength, KTX_TEXTURE_CREATE_CHECK_GLTF_BASISU_BIT, &kTexture);
                        if (KTX_SUCCESS != result) return false;
                        result = ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0);
                        if (KTX_SUCCESS != result) return false;
                        pixels = ktxTexture_GetData(ktxTexture(kTexture));

                        ktx_size_t offset;
                        ktxTexture_GetImageOffset(ktxTexture(kTexture), 0, 0, 0, &offset);
                        pixels += offset;
                        size = static_cast<uint32_t>(ktxTexture_GetImageSize(ktxTexture(kTexture), 0));

                        format = static_cast<VkFormat>(kTexture->vkFormat);
                        width = kTexture->baseWidth;
                        height = kTexture->baseHeight;
                        miplevel = 1;
                        return true;
                    };
                    
                    if (!loadKtxFromMemory())
                    {
                        SPDLOG_ERROR("load texture {} failed.", texname);
                    }
#endif
                }
                else if (mime.find("image/webp") != std::string::npos)
                {
                     stbdata = WebPDecodeRGBA(copyedData, bytelength, &width, &height);
                     if (stbdata)
                     {
                         size = width * height * 4;
                         pixels = stbdata;
                         format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                         miplevel = 1;
                         useWebPFree = true;
                     }
                     else
                     {
                         SPDLOG_ERROR("Failed to decode WebP image: {}", texname);
                     }
                }
                else
                {
                    std::hash<std::string> hasher;
                    // load from texture files
                    if (hdr)
                    {
                        const FHDRTexturePayload payload = LoadHDRTexturePayload(texname, copyedData, bytelength);
                        width = payload.Width;
                        height = payload.Height;
                        channels = 4;
                        miplevel = initialHDRResidency == EHDRTextureResidency::FullMip
                            ? std::max<uint32_t>(1, payload.MipLevels)
                            : 1;
                        format = payload.Format;
                        hdrSphericalHarmonics_[newTextureIdx] = payload.SH;
                        textureImages_[newTextureIdx] = CreateHDRTextureImage(commandPool_, payload, initialHDRResidency);
                        textureCreated = textureImages_[newTextureIdx] != nullptr;
                    }

                    if (hdr && !textureCreated)
                    {
                        if (copyedData == nullptr || bytelength == 0)
                        {
                            createHdrPlaceholderTexture();
                        }

                        std::string cacheFileName = Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", hasher(texname)), "texhdr");
                        std::filesystem::path cacheFilePath(cacheFileName);
                        bool cacheLoaded = false;
                        std::vector<std::vector<float>> mipLevels;
                        std::vector<std::pair<int, int>> mipDimensions;

                        if (!textureCreated && std::filesystem::exists(cacheFilePath))
                        {
                            std::ifstream cacheFile(cacheFileName, std::ios::binary);
                            if (cacheFile.is_open())
                            {
                                HdrCacheHeader header{};
                                cacheFile.read(reinterpret_cast<char*>(&header), sizeof(header));

                                bool validCache = cacheFile.gcount() == sizeof(header)
                                    && header.magic == kHdrCacheMagic
                                    && header.version == kHdrCacheVersion
                                    && header.originalSize > 0
                                    && header.compressedSize > 0
                                    && header.originalSize <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())
                                    && header.compressedSize <= static_cast<uint64_t>(std::numeric_limits<size_t>::max())
                                    && header.originalSize <= static_cast<uint64_t>(std::numeric_limits<int>::max())
                                    && header.compressedSize <= static_cast<uint64_t>(std::numeric_limits<int>::max());

                                if (validCache)
                                {
                                    size_t compressedSize = static_cast<size_t>(header.compressedSize);
                                    size_t originalSize = static_cast<size_t>(header.originalSize);

                                    std::vector<uint8_t> compressedData(compressedSize);
                                    cacheFile.read(reinterpret_cast<char*>(compressedData.data()), compressedSize);
                                    if (!cacheFile)
                                    {
                                        validCache = false;
                                    }
                                    else
                                    {
                                        std::vector<uint8_t> uncompressedData(originalSize);
                                        size_t decompressedSize = lzav_decompress(
                                            compressedData.data(), uncompressedData.data(),
                                            static_cast<int>(compressedSize), static_cast<int>(originalSize));

                                        if (decompressedSize != originalSize
                                            || HashBuffer(uncompressedData.data(), uncompressedData.size()) != header.dataHash)
                                        {
                                            validCache = false;
                                        }
                                        else
                                        {
                                            size_t offset = 0;
                                            auto readFromBuffer = [&](void* dst, size_t readSize) -> bool
                                            {
                                                if (offset + readSize > uncompressedData.size())
                                                {
                                                    return false;
                                                }
                                                std::memcpy(dst, uncompressedData.data() + offset, readSize);
                                                offset += readSize;
                                                return true;
                                            };

                                            SphericalHarmonics sh{};
                                            size_t mipCount = 0;

                                            if (!readFromBuffer(&width, sizeof(int))
                                                || !readFromBuffer(&height, sizeof(int))
                                                || !readFromBuffer(&miplevel, sizeof(uint32_t))
                                                || !readFromBuffer(&sh, sizeof(SphericalHarmonics))
                                                || !readFromBuffer(&mipCount, sizeof(size_t)))
                                            {
                                                validCache = false;
                                            }
                                            else
                                            {
                                                hdrSphericalHarmonics_[newTextureIdx] = sh;
                                                mipDimensions.resize(mipCount);
                                                for (auto& dim : mipDimensions)
                                                {
                                                    if (!readFromBuffer(&dim.first, sizeof(int))
                                                        || !readFromBuffer(&dim.second, sizeof(int)))
                                                    {
                                                        validCache = false;
                                                        break;
                                                    }
                                                }
                                            }

                                            if (validCache)
                                            {
                                                format = VK_FORMAT_R32G32B32A32_SFLOAT;
                                                size = width * height * 4 * sizeof(float);
                                                stbdata = reinterpret_cast<uint8_t*>(malloc(size));
                                                pixels = stbdata;
                                                if (!readFromBuffer(pixels, size))
                                                {
                                                    validCache = false;
                                                }
                                            }

                                            if (validCache)
                                            {
                                                mipLevels.clear();
                                                mipLevels.resize(mipCount);
                                                for (auto& mipData : mipLevels)
                                                {
                                                    size_t mipSize = 0;
                                                    if (!readFromBuffer(&mipSize, sizeof(size_t)))
                                                    {
                                                        validCache = false;
                                                        break;
                                                    }
                                                    mipData.resize(mipSize);
                                                    if (!readFromBuffer(mipData.data(), mipSize * sizeof(float)))
                                                    {
                                                        validCache = false;
                                                        break;
                                                    }
                                                }
                                            }

                                            if (validCache)
                                            {
                                                textureImages_[newTextureIdx] = std::make_unique<TextureImage>(
                                                    commandPool_, width, height, miplevel, format,
                                                    pixels, size, mipLevels, mipDimensions);
                                                cacheLoaded = true;
                                                textureCreated = true;
                                            }
                                        }
                                    }
                                }

                                cacheFile.close();
                            }

                            if (!cacheLoaded)
                            {
                                std::error_code removeError;
                                std::filesystem::remove(cacheFilePath, removeError);
                            }
                        }

                        if (!textureCreated && !cacheLoaded)
                        {
                            stbdata = reinterpret_cast<uint8_t*>(stbi_loadf_from_memory(
                                copyedData, static_cast<uint32_t>(bytelength), &width, &height, &channels, STBI_rgb_alpha));
                            if (stbdata == nullptr)
                            {
                                createHdrPlaceholderTexture();
                            }
                            else
                            {
                                pixels = stbdata;
                                format = VK_FORMAT_R32G32B32A32_SFLOAT;
                                size = width * height * 4 * sizeof(float);

                                SphericalHarmonics sh = ProjectHdrToSh((float*)pixels, width, height);
                                hdrSphericalHarmonics_[newTextureIdx] = sh;

                                PrefilterHdrEnvironmentMap((float*)pixels, width, height, mipLevels, mipDimensions);
                                miplevel = static_cast<uint32_t>(mipLevels.size());

                                std::vector<uint8_t> uncompressedData;
                                auto writeToBuffer = [&](const void* src, size_t writeSize)
                                {
                                    const uint8_t* bytes = static_cast<const uint8_t*>(src);
                                    uncompressedData.insert(uncompressedData.end(), bytes, bytes + writeSize);
                                };

                                writeToBuffer(&width, sizeof(int));
                                writeToBuffer(&height, sizeof(int));
                                writeToBuffer(&miplevel, sizeof(uint32_t));
                                writeToBuffer(&sh, sizeof(SphericalHarmonics));

                                size_t mipCount = mipDimensions.size();
                                writeToBuffer(&mipCount, sizeof(size_t));
                                for (const auto& dim : mipDimensions)
                                {
                                    writeToBuffer(&dim.first, sizeof(int));
                                    writeToBuffer(&dim.second, sizeof(int));
                                }

                                writeToBuffer(pixels, size);

                                for (const auto& mipData : mipLevels)
                                {
                                    size_t mipSize = mipData.size();
                                    writeToBuffer(&mipSize, sizeof(size_t));
                                    writeToBuffer(mipData.data(), mipSize * sizeof(float));
                                }

                                size_t uncompressedSize = uncompressedData.size();
                                if (uncompressedSize <= static_cast<size_t>(std::numeric_limits<int>::max()))
                                {
                                    size_t compressedBound = lzav_compress_bound_hi(int(uncompressedSize));
                                    std::vector<uint8_t> compressedData(compressedBound);
                                    size_t actualCompressedSize = lzav_compress_hi(
                                        uncompressedData.data(), compressedData.data(),
                                        int(uncompressedSize), int(compressedBound));

                                    if (actualCompressedSize > 0)
                                    {
                                        HdrCacheHeader header{};
                                        header.magic = kHdrCacheMagic;
                                        header.version = kHdrCacheVersion;
                                        header.originalSize = static_cast<uint64_t>(uncompressedSize);
                                        header.compressedSize = static_cast<uint64_t>(actualCompressedSize);
                                        header.dataHash = HashBuffer(uncompressedData.data(), uncompressedData.size());

                                        std::filesystem::path tempCachePath = cacheFilePath;
                                        tempCachePath += ".tmp";

                                        std::ofstream cacheFile(tempCachePath, std::ios::binary | std::ios::trunc);
                                        if (cacheFile.is_open())
                                        {
                                            cacheFile.write(reinterpret_cast<const char*>(&header), sizeof(header));
                                            cacheFile.write(reinterpret_cast<const char*>(compressedData.data()), actualCompressedSize);
                                            cacheFile.flush();
                                            cacheFile.close();

                                            std::error_code removeError;
                                            std::filesystem::remove(cacheFilePath, removeError);

                                            std::error_code renameError;
                                            std::filesystem::rename(tempCachePath, cacheFilePath, renameError);
                                            if (renameError)
                                            {
                                                std::filesystem::remove(tempCachePath);
                                            }
                                        }
                                        else
                                        {
                                            std::error_code removeTempError;
                                            std::filesystem::remove(tempCachePath, removeTempError);
                                        }
                                    }
                                }

                                textureImages_[newTextureIdx] = std::make_unique<TextureImage>(
                                    commandPool_, width, height, miplevel, format,
                                    pixels, size, mipLevels, mipDimensions);
                                textureCreated = true;
                            }
                        }
                    }
                    else if (!hdr)
                    {
#if WITH_KTX2
                        // ldr texture, try cache fist
                        // hash the texname
                        std::string cacheFileName = Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", hasher(texname)), "texktx");
                        if (!std::filesystem::exists(cacheFileName))
                        {
                            // load from stbi and compress to ktx and cache
                            stbdata = stbi_load_from_memory(copyedData, static_cast<uint32_t>(bytelength), &width, &height, &channels, STBI_rgb_alpha);
                            format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                            size = width * height * 4 * sizeof(uint8_t);

                            ktxTextureCreateInfo createInfo = {
                                0,
                                static_cast<uint32_t>(format),
                                0,
                                static_cast<uint32_t>(width),
                                static_cast<uint32_t>(height),
                                1, 2, 1, 1, 1,KTX_FALSE,KTX_FALSE
                            };

                            result = ktxTexture2_Create(&createInfo, KTX_TEXTURE_CREATE_ALLOC_STORAGE, &kTexture);
                            if (result != KTX_SUCCESS) Throw(std::runtime_error("failed to create ktx2 image "));

                            std::memcpy(ktxTexture_GetData(ktxTexture(kTexture)), stbdata, size);

                            ktxBasisParams params = {};
                            params.structSize = sizeof(params);
                            params.uastc = KTX_TRUE;
                            params.compressionLevel = 2;
                            params.qualityLevel = 128;
                            params.threadCount = 12;
                            result = ktxTexture2_CompressBasisEx(kTexture, &params);
                            if (KTX_SUCCESS != result) Throw(std::runtime_error("failed to compress ktx2 image "));
                            // save to cache
                            ktxTexture_WriteToNamedFile(ktxTexture(kTexture), cacheFileName.c_str());
                        }
                        else
                        {
                            result = ktxTexture2_CreateFromNamedFile(cacheFileName.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);
                            if (result != KTX_SUCCESS) Throw(std::runtime_error("failed to load ktx2 image "));
                        }

                        // next
                        result = ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0);
                        if (result != KTX_SUCCESS) Throw(std::runtime_error("failed to transcode ktx2 image "));

                        pixels = ktxTexture_GetData(ktxTexture(kTexture));
                        ktx_size_t offset;
                        ktxTexture_GetImageOffset(ktxTexture(kTexture), 0, 0, 0, &offset);
                        pixels += offset;
                        size = static_cast<uint32_t>(ktxTexture_GetImageSize(ktxTexture(kTexture), 0));

                        format = srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK;
                        width = kTexture->baseWidth;
                        height = kTexture->baseHeight;
                        miplevel = 1;
#endif
                    }
                }

                // create texture image
                if (!hdr)
                {
                    if (pixels == nullptr || size == 0)
                    {
                        static constexpr std::array<uint8_t, 4> kPlaceholderPixel = {255, 255, 255, 255};
                        width = 1;
                        height = 1;
                        miplevel = 1;
                        format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                        pixels = const_cast<uint8_t*>(kPlaceholderPixel.data());
                        size = static_cast<uint32_t>(kPlaceholderPixel.size());
                    }
                    textureImages_[newTextureIdx] = std::make_unique<TextureImage>(commandPool_, width, height, miplevel, format, pixels, size);
                    textureCreated = true;
                }

                if (!textureCreated)
                {
                    throw std::runtime_error(fmt::format("failed to create texture '{}'", texname));
                }

                textureImages_[newTextureIdx]->SetDebugName(fmt::format("Texture {}", texname));
                BindTexture(newTextureIdx, *(textureImages_[newTextureIdx]));

                // clean up
                if (stbdata)
                {
                    if (useWebPFree) WebPFree(stbdata);
                    else stbi_image_free(stbdata);
                }
                
#if WITH_KTX2
                if (kTexture) ktxTexture_Destroy(ktxTexture(kTexture));
#endif
                
                // transfer
                taskContext.textureId = newTextureIdx;
                taskContext.needFlushHDRSH = hdr;
                taskContext.hdrResidency = static_cast<uint8_t>(initialHDRResidency);
                taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - timer).count();
                std::string info = hdr
                    ? fmt::format("loaded {} ({} x {} x {}, {}) in {:.2f}ms", texname, width, height, miplevel,
                                  HDRResidencyName(initialHDRResidency), taskContext.elapsed * 1000.f)
                    : fmt::format("loaded {} ({} x {} x {}) in {:.2f}ms", texname, width, height, miplevel,
                                  taskContext.elapsed * 1000.f);
                std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
                task.SetContext(taskContext);
            };

        auto textureCompleteTask = [this, copyedData](Tasks::ResTask& task)
            {
                TextureTaskContext taskContext{};
                task.GetContext(taskContext);
                textureImages_[taskContext.textureId]->MainThreadPostLoading(mainThreadCommandPool_);
                SPDLOG_INFO("{}", taskContext.outputInfo.data());
                delete[] copyedData;

                if (taskContext.needFlushHDRSH)
                {
                    if (static_cast<size_t>(taskContext.textureId) < hdrTextureResidency_.size())
                    {
                        auto& residency = hdrTextureResidency_[taskContext.textureId];
                        residency.Current = static_cast<EHDRTextureResidency>(taskContext.hdrResidency);
                        residency.Target = residency.Current;
                        residency.Pending = false;
                    }
                    NextEngine::GetInstance()->GetScene().UpdateHDRSH();
                }
            };

        if (textureWorkerUploadEnabled_)
        {
            Tasks::TaskCoordinator::GetInstance()->AddTask(std::move(textureLoadTask), std::move(textureCompleteTask), 0);
        }
        else
        {
            Tasks::TaskCoordinator::GetInstance()->AddMainThreadTask(std::move(textureLoadTask), std::move(textureCompleteTask), 0);
        }

        return newTextureIdx;
    }

    void GlobalTexturePool::TickHDRTextureResidency(
        uint32_t activeTextureIdx, bool hasSky, uint32_t frameIndex, bool streamingEnabled)
    {
        if (!streamingEnabled)
        {
            for (uint32_t textureIdx = 0; textureIdx < hdrTextureResidency_.size(); ++textureIdx)
            {
                const auto& residency = hdrTextureResidency_[textureIdx];
                if (residency.IsHDR && !residency.Pending && residency.Current != EHDRTextureResidency::FullMip)
                {
                    QueueHDRTextureResidency(textureIdx, EHDRTextureResidency::FullMip);
                }
            }
            return;
        }

        for (uint32_t textureIdx = 0; textureIdx < hdrTextureResidency_.size(); ++textureIdx)
        {
            auto& residency = hdrTextureResidency_[textureIdx];
            if (!residency.IsHDR)
            {
                continue;
            }

            const bool touched = hasSky && textureIdx == activeTextureIdx;
            if (touched)
            {
                residency.DemandFrames =
                    (residency.LastTouchedFrame + 1 == frameIndex) ? residency.DemandFrames + 1 : 1;
                residency.LastTouchedFrame = frameIndex;

                if (!residency.Pending
                    && residency.Current == EHDRTextureResidency::LowestMip
                    && residency.DemandFrames >= kHdrTexturePromotionFrames)
                {
                    QueueHDRTextureResidency(textureIdx, EHDRTextureResidency::FullMip);
                }
                continue;
            }

            if (!residency.Pending
                && residency.Current == EHDRTextureResidency::FullMip
                && frameIndex > residency.LastTouchedFrame + kHdrTextureDemotionFrames)
            {
                QueueHDRTextureResidency(textureIdx, EHDRTextureResidency::LowestMip);
            }
        }
    }

    void GlobalTexturePool::QueueHDRTextureResidency(uint32_t textureIdx, EHDRTextureResidency targetResidency)
    {
        if (textureIdx >= hdrTextureResidency_.size()
            || textureIdx >= textureImages_.size()
            || !hdrTextureResidency_[textureIdx].IsHDR)
        {
            return;
        }

        auto& residency = hdrTextureResidency_[textureIdx];
        if (residency.Pending || residency.Current == targetResidency)
        {
            return;
        }

        residency.Pending = true;
        residency.Target = targetResidency;
        const std::string textureName = residency.TextureName;

        auto sourceData = std::make_shared<std::vector<uint8_t>>();
        if (!std::filesystem::exists(HDRCacheFileName(textureName)))
        {
            Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(textureName, *sourceData);
        }

        auto textureResidencyTask =
            [this, textureIdx, textureName, targetResidency, sourceData](Tasks::ResTask& task)
            {
                TextureTaskContext taskContext {};
                const auto timer = std::chrono::high_resolution_clock::now();

                const uint8_t* data = sourceData->empty() ? nullptr : sourceData->data();
                const FHDRTexturePayload payload = LoadHDRTexturePayload(textureName, data, sourceData->size());
                auto textureImage = CreateHDRTextureImage(commandPool_, payload, targetResidency);
                textureImage->SetDebugName(fmt::format("Texture {}", textureName));

                device_.WaitIdle();
                textureImages_[textureIdx] = std::move(textureImage);
                hdrSphericalHarmonics_[textureIdx] = payload.SH;

                taskContext.textureId = static_cast<int32_t>(textureIdx);
                taskContext.needFlushHDRSH = true;
                taskContext.hdrResidency = static_cast<uint8_t>(targetResidency);
                taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - timer).count();
                const std::string info = fmt::format(
                    "[HDRTextureResidency] {} -> {} in {:.2f}ms",
                    textureName, HDRResidencyName(targetResidency), taskContext.elapsed * 1000.f);
                std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
                task.SetContext(taskContext);
            };

        auto textureResidencyCompleteTask = [this](Tasks::ResTask& task)
            {
                TextureTaskContext taskContext {};
                task.GetContext(taskContext);
                const uint32_t textureIdx = static_cast<uint32_t>(taskContext.textureId);
                if (textureIdx < textureImages_.size() && textureImages_[textureIdx])
                {
                    textureImages_[textureIdx]->MainThreadPostLoading(mainThreadCommandPool_);
                    BindTexture(textureIdx, *textureImages_[textureIdx]);
                }

                if (textureIdx < hdrTextureResidency_.size())
                {
                    auto& residency = hdrTextureResidency_[textureIdx];
                    residency.Current = static_cast<EHDRTextureResidency>(taskContext.hdrResidency);
                    residency.Target = residency.Current;
                    residency.Pending = false;
                    if (residency.Current == EHDRTextureResidency::LowestMip)
                    {
                        residency.DemandFrames = 0;
                    }
                }

                SPDLOG_INFO("{}", taskContext.outputInfo.data());
                NextEngine::GetInstance()->GetScene().UpdateHDRSH();
            };

        if (textureWorkerUploadEnabled_)
        {
            Tasks::TaskCoordinator::GetInstance()->AddTask(
                std::move(textureResidencyTask), std::move(textureResidencyCompleteTask), 0);
        }
        else
        {
            Tasks::TaskCoordinator::GetInstance()->AddMainThreadTask(
                std::move(textureResidencyTask), std::move(textureResidencyCompleteTask), 0);
        }
    }

    void GlobalTexturePool::FreeTransientTextures()
    {
        // make sure the binded image not in use
        device_.WaitIdle();
        
        for (auto& textureGroup : textureNameMap_)
        {
            if (textureGroup.second.Lifetime_ == ETextureLifetime::ETL_Persistent)
            {
                continue;
            }

            const uint32_t textureIdx = textureGroup.second.GlobalIdx_;
            if (textureIdx >= textureImages_.size())
            {
                continue;
            }

            if (textureImages_[textureIdx])
            {
                textureImages_[textureIdx].reset();
                BindTexture(textureIdx, *defaultWhiteTexture_);
            }

            textureGroup.second.Status_ = ETextureStatus::ETS_Unloaded;
        }
    }

    void GlobalTexturePool::CreateDefaultTextures()
    {
        defaultWhiteTexture_ = std::make_unique<TextureImage>(commandPool_, 16, 16, 1, VK_FORMAT_R8G8B8A8_UNORM, nullptr, 0);
        defaultWhiteTexture_->SetDebugName("Texture DefaultWhite");
    }

    GlobalTexturePool* GlobalTexturePool::instance_ = nullptr;
}
