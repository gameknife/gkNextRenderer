#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/GPU/HdrTextureCache.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Utilities/StbImage.hpp"
#include "ThirdParty/lzav/lzav.h"

#include <limits>
#include <system_error>

#define M_NEXT_PI 3.14159265358979323846f

namespace
{
    constexpr uint32_t kHdrCacheMagic = 0x48445243; // 'HDRC'
    constexpr uint32_t kHdrCacheVersion = 2;

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

}

namespace Assets
{
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
                // Match Common.SampleIBL's atan2(direction.x, direction.z) mapping.
                float dx = sinTheta * sinPhi;
                float dy = cosTheta;
                float dz = sinTheta * cosPhi;
                
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
        // A missing or undecodable HDR yields a default-constructed payload. Creating a
        // 0x0 VK_FORMAT_UNDEFINED image from it fails inside VMA and throws off a worker
        // thread, so report the failure and let the caller install its placeholder.
        if (payload.Width <= 0 || payload.Height <= 0
            || payload.Format == VK_FORMAT_UNDEFINED
            || payload.BasePixels.empty())
        {
            return nullptr;
        }

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

}
