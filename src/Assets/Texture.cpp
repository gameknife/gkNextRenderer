#include "Texture.hpp"
#include "Utilities/StbImage.hpp"
#include "Utilities/Exception.hpp"
#include <chrono>
#include <imgui_impl_vulkan.h>
#include <fmt/format.h>
#include <ktx.h>

#include "Options.hpp"
#include "Runtime/TaskCoordinator.hpp"
#include "TextureImage.hpp"
#include "Runtime/Engine.hpp"
#include "Utilities/FileHelper.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/ImageView.hpp"
#include "Vulkan/DescriptorBinding.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorSets.hpp"

#define M_NEXT_PI 3.14159265358979323846f

namespace Assets
{
    struct TextureTaskContext
    {
        int32_t textureId;
        TextureImage* transferPtr;
        float elapsed;
        bool needFlushHDRSH;
        std::array<char, 256> outputInfo;
    };
    
    void PrefilterEnvironmentMapLevel(const float* sourcePixels, int sourceWidth, int sourceHeight,
                                    float* targetPixels, int targetWidth, int targetHeight, 
                                    float roughness)
    {
        const int sampleCount = std::max(1, static_cast<int>(128 * (1.0f - roughness) + 64 * roughness));
        
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

    void PrefilterHDREnvironmentMap(const float* hdrPixels, int width, int height, 
                             std::vector<std::vector<float>>& mipLevels,
                             std::vector<std::pair<int, int>>& mipDimensions)
    {
        constexpr int MAX_MIP_LEVELS = 8; // Typically 5-8 levels for environment maps
        mipLevels.clear();
        mipDimensions.clear();
        
        // Calculate mip levels
        int currentWidth = width;
        int currentHeight = height;
        
        for (int mipLevel = 0; mipLevel < MAX_MIP_LEVELS; ++mipLevel)
        {
            if (currentWidth < 4 || currentHeight < 4) break;
            
            mipDimensions.push_back({currentWidth, currentHeight});
            mipLevels.emplace_back(currentWidth * currentHeight * 4); // RGBA

            if (mipLevel > 0)
            {
                float roughness = static_cast<float>(mipLevel) / (MAX_MIP_LEVELS - 1);
                PrefilterEnvironmentMapLevel(hdrPixels, width, height, 
                                           mipLevels[mipLevel].data(), 
                                           currentWidth, currentHeight, roughness);
            }
            
            currentWidth = std::max(1, currentWidth / 2);
            currentHeight = std::max(1, currentHeight / 2);
        }
    }

    SphericalHarmonics ProjectHDRToSH(const float* hdrPixels, int width, int height)
    {
        SphericalHarmonics result{};
        
        // Initialize coefficients to zero
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 9; ++j)
                result.coefficients[i][j] = 0.0f;
        
        // SH basis function evaluation constants
        constexpr float SH_C0 = 0.282095f; // 1/(2*sqrt(π))
        constexpr float SH_C1 = 0.488603f; // sqrt(3)/(2*sqrt(π))
        constexpr float SH_C2 = 1.092548f; // sqrt(15)/(2*sqrt(π))
        constexpr float SH_C3 = 0.315392f; // sqrt(5)/(4*sqrt(π))
        constexpr float SH_C4 = 0.546274f; // sqrt(15)/(4*sqrt(π))
        
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
                basis[0] = SH_C0;
                
                // Band 1 (3 coefficients)
                basis[1] = -SH_C1 * dy;
                basis[2] = SH_C1 * dz;
                basis[3] = -SH_C1 * dx;
                
                // Band 2 (5 coefficients)
                basis[4] = SH_C2 * dx * dy;
                basis[5] = -SH_C2 * dy * dz;
                basis[6] = SH_C3 * (3.0f * dy * dy - 1.0f);
                basis[7] = -SH_C2 * dx * dz;
                basis[8] = SH_C4 * (dx * dx - dz * dz);
                
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

    uint32_t GlobalTexturePool::LoadTexture(const std::string& filename, bool srgb)
    {
        std::vector<uint8_t> data;
        Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(filename, data);
        std::filesystem::path path(filename);
        std::string mime = std::string("image/") + path.extension().string().substr(1);
        return GetInstance()->RequestNewTextureMemAsync(filename, mime, false, data.data(), data.size(),srgb);
    }

    uint32_t GlobalTexturePool::LoadTexture(const std::string& texname, const std::string& mime,
                                            const unsigned char* data, size_t bytelength, bool srgb)
    {
        return GetInstance()->RequestNewTextureMemAsync(texname, mime, false, data, bytelength, srgb);
    }

    uint32_t GlobalTexturePool::LoadHDRTexture(const std::string& filename)
    {
        std::vector<uint8_t> data;
        Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(filename, data);
        return GetInstance()->RequestNewTextureMemAsync(filename, "image/hdr", true, data.data(), data.size(),false);
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

    GlobalTexturePool::GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& command_pool,
                                         Vulkan::CommandPool& command_pool_mt) :
        device_(device),
        commandPool_(command_pool),
        mainThreadCommandPool_(command_pool_mt)
    {
        static const uint32_t k_max_bindless_resources = 65535u;// moltenVK returns a invalid value. std::min(65535u, device.DeviceProperties().limits.maxPerStageDescriptorSamplers);
        const std::vector<Vulkan::DescriptorBinding> descriptorBindings =
        {
            {0, k_max_bindless_resources, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_ALL},
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
        std::vector<VkWriteDescriptorSet> descriptorWrites =
        {
            descriptorSets.Bind(0, 0, { textureImage.Sampler().Handle(), textureImage.ImageView().Handle(), VK_IMAGE_LAYOUT_GENERAL}, textureIdx, 1),
        };
        descriptorSets.UpdateDescriptors(0, descriptorWrites);
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
                                                          const unsigned char* data, size_t bytelength, bool srgb)
    {
        uint32_t newTextureIdx = 0;
        if (textureNameMap_.find(texname) != textureNameMap_.end())
        {
            // 这里要判断一下，如果TextureUnLoaded，重新绑定
            if(textureNameMap_[texname].Status_ == ETextureStatus::ETS_Unloaded)
            {
                textureNameMap_[texname].Status_ = ETextureStatus::ETS_Loaded;
                newTextureIdx = textureNameMap_[texname].GlobalIdx_;
            }
            else
            {
                // 这里要判断一下，如果已经加载了，直接返回
                return textureNameMap_[texname].GlobalIdx_;
            }
        }
        else
        {
            textureImages_.emplace_back(nullptr);
            newTextureIdx = static_cast<uint32_t>(textureImages_.size()) - 1;
            textureNameMap_[texname] = { newTextureIdx, ETextureStatus::ETS_Loaded };
        }

        // load parse bind texture into newTextureIdx with transfer queue

        uint8_t* copyedData = new uint8_t[bytelength];
        memcpy(copyedData, data, bytelength);
        TaskCoordinator::GetInstance()->AddTask(
            [this, hdr, srgb, texname, mime, copyedData, bytelength, newTextureIdx](ResTask& task)
            {
                TextureTaskContext taskContext{};
                const auto timer = std::chrono::high_resolution_clock::now();

                // Load the texture in normal host memory.
                int width, height, channels;
                uint8_t* stbdata = nullptr;
                uint8_t* pixels = nullptr;
                uint32_t size = 0;
                uint32_t miplevel = 1;
                VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

                ktxTexture2* kTexture = nullptr;
                ktx_error_code_e result;
                // load from ktx inside glb
                if (mime.find("image/ktx") != std::string::npos)
                {
                    result = ktxTexture2_CreateFromMemory(copyedData, bytelength, KTX_TEXTURE_CREATE_CHECK_GLTF_BASISU_BIT, &kTexture);
                    if (KTX_SUCCESS != result) Throw(std::runtime_error("failed to load ktx2 texture image "));
                    result = ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0);
                    if (KTX_SUCCESS != result) Throw(std::runtime_error("failed to load ktx2 texture image "));
                    pixels = ktxTexture_GetData(ktxTexture(kTexture));

                    ktx_size_t offset;
                    ktxTexture_GetImageOffset(ktxTexture(kTexture), 0, 0, 0, &offset);
                    pixels += offset;
                    size = static_cast<uint32_t>(ktxTexture_GetImageSize(ktxTexture(kTexture), 0));

                    format = static_cast<VkFormat>(kTexture->vkFormat);
                    width = kTexture->baseWidth;
                    height = kTexture->baseHeight;
                    miplevel = 1;
                }
                else
                {
                    std::filesystem::path cachePath = Utilities::FileHelper::GetPlatformFilePath("cache");
                    std::filesystem::create_directories(cachePath);
                    std::hash<std::string> hasher;
                    std::string hashname = fmt::format("{:x}", hasher(texname));
                    // load from texture files
                    if (hdr)
                    {
                        std::string cacheFileName = (cachePath / fmt::format("{}.cookhdr", hashname)).string();
                        if (!std::filesystem::exists(cacheFileName))
                        {
                            stbdata = reinterpret_cast<uint8_t*>(stbi_loadf_from_memory(copyedData, static_cast<uint32_t>(bytelength), &width, &height, &channels, STBI_rgb_alpha));
                            pixels = stbdata;
                            format = VK_FORMAT_R32G32B32A32_SFLOAT;
                            size = width * height * 4 * sizeof(float);

                            // Extract spherical harmonics from the base level
                            SphericalHarmonics sh = ProjectHDRToSH((float*)pixels, width, height);
                            hdrSphericalHarmonics_[newTextureIdx] = sh;

                            // NEW: Prefilter environment map for different roughness levels
                            std::vector<std::vector<float>> mipLevels;
                            std::vector<std::pair<int, int>> mipDimensions;
                            PrefilterHDREnvironmentMap((float*)pixels, width, height, mipLevels, mipDimensions);

                            miplevel = static_cast<uint32_t>( mipLevels.size() );
                            // Create texture image with multiple mip levels
                            textureImages_[newTextureIdx] = std::make_unique<TextureImage>(
                                commandPool_, width, height, miplevel, format, 
                                pixels, size, mipLevels, mipDimensions);
                        }
                        else
                        {
                            // load directly from cooked hdr
                        }
                        // can cache to disk, next round will create image directly
                    }
                    else
                    {
                        // ldr texture, try cache fist
                        // hash the texname
                        std::string cacheFileName = (cachePath / fmt::format("{}.ktx", hashname)).string();
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
                            ktxTexture_WriteToNamedFile(ktxTexture(kTexture), (cachePath / fmt::format("{}.ktx", hashname)).string().c_str());
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
                    }
                }

                // create texture image
                //if ( !textureImages_[newTextureIdx] )
                if (!hdr)
                {
                    textureImages_[newTextureIdx] = std::make_unique<TextureImage>(commandPool_, width, height, miplevel, format, pixels, size);
                }

                BindTexture(newTextureIdx, *(textureImages_[newTextureIdx]));

                // clean up
                if (stbdata) stbi_image_free(stbdata);
                if (kTexture)
                    ktxTexture_Destroy(ktxTexture(kTexture));

                // transfer
                taskContext.textureId = newTextureIdx;
                taskContext.needFlushHDRSH = hdr;
                taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - timer).count();
                std::string info = fmt::format("loaded {} ({} x {} x {}) in {:.2f}ms", texname, width, height, miplevel,
                                               taskContext.elapsed * 1000.f);
                std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
                task.SetContext(taskContext);
            }, [this, copyedData](ResTask& task)
            {
                TextureTaskContext taskContext{};
                task.GetContext(taskContext);
                textureImages_[taskContext.textureId]->MainThreadPostLoading(mainThreadCommandPool_);
                fmt::print("{}\n", taskContext.outputInfo.data());
                delete[] copyedData;

                if (taskContext.needFlushHDRSH)
                {
                    NextEngine::GetInstance()->GetScene().UpdateHDRSH();
                }
            }, 0);

        return newTextureIdx;
    }

    void GlobalTexturePool::FreeNonSystemTextures()
    {
        // make sure the binded image not in use
        device_.WaitIdle();
        
        for( int i = 0; i < textureImages_.size(); ++i)
        {
            if( i > 10 )
            {
                // free up TextureImage;, rebind with a default texture sampler
                textureImages_[i].reset();
                BindTexture(i, *defaultWhiteTexture_);
            }
        }

        for( auto& textureGroup : textureNameMap_ )
        {
            if( textureGroup.second.GlobalIdx_ > 10 )
            {
                textureGroup.second.Status_ = ETextureStatus::ETS_Unloaded;
            }
        }
    }

    void GlobalTexturePool::CreateDefaultTextures()
    {
        defaultWhiteTexture_ = std::make_unique<TextureImage>(commandPool_, 16, 16, 1, VK_FORMAT_R8G8B8A8_UNORM, nullptr, 0);
    }

    GlobalTexturePool* GlobalTexturePool::instance_ = nullptr;
}
