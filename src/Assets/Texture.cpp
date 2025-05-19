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
#include "Utilities/FileHelper.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/ImageView.hpp"

#define M_NEXT_PI 3.14159265358979323846f

namespace Assets
{
    struct TextureTaskContext
    {
        int32_t textureId;
        TextureImage* transferPtr;
        float elapsed;
        std::array<char, 256> outputInfo;
    };

    void FilterEnvironmentMap(const float* srcPixels, int srcWidth, int srcHeight,
                         float* dstPixels, int dstWidth, int dstHeight, float roughness) {
        // Constants for filtering
        const int sampleCount = 16;  // Number of samples to take for each output pixel
        const float roughness2 = roughness * roughness;
        
        // For each destination pixel
        for (int y = 0; y < dstHeight; ++y) {
            for (int x = 0; x < dstWidth; ++x) {
                // Calculate spherical coordinates for this pixel
                float u = (x + 0.5f) / dstWidth;
                float v = (y + 0.5f) / dstHeight;
                float phi = u * 2.0f * M_NEXT_PI;
                float theta = v * M_NEXT_PI;
                
                // Direction vector
                float dx = std::sin(theta) * std::cos(phi);
                float dy = std::cos(theta);
                float dz = std::sin(theta) * std::sin(phi);
                
                // Accumulate filtered color
                float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
                float weight = 0.0f;
                
                // Monte Carlo integration with importance sampling
                for (int i = 0; i < sampleCount; ++i) {
                    // Generate sample direction with roughness-based lobe
                    float u1 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
                    float u2 = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
                    
                    // Calculate sample direction with GGX distribution
                    float phi_sample = 2.0f * M_NEXT_PI * u1;
                    float cos_theta_sample = std::sqrt((1.0f - u2) / (1.0f + (roughness2 - 1.0f) * u2));
                    float sin_theta_sample = std::sqrt(1.0f - cos_theta_sample * cos_theta_sample);
                    
                    // Convert to Cartesian
                    float sx = sin_theta_sample * std::cos(phi_sample);
                    float sy = cos_theta_sample;
                    float sz = sin_theta_sample * std::sin(phi_sample);
                    
                    // Transform sample to world space around our main direction
                    // (This is a simplified orthonormal basis construction)
                    float rx, ry, rz;
                    // ... (rotate sx, sy, sz around dx, dy, dz to get rx, ry, rz)
                    // Simplified version that doesn't account for proper rotation:
                    rx = dx + sx * roughness;
                    ry = dy + sy * roughness;
                    rz = dz + sz * roughness;
                    float len = std::sqrt(rx*rx + ry*ry + rz*rz);
                    rx /= len; ry /= len; rz /= len;
                    
                    // Convert to UV coordinates for sampling source
                    float sample_phi = std::atan2(rz, rx);
                    if (sample_phi < 0) sample_phi += 2.0f * M_NEXT_PI;
                    float sample_theta = std::acos(std::clamp(ry, -1.0f, 1.0f));
                    
                    float sample_u = sample_phi / (2.0f * M_NEXT_PI);
                    float sample_v = sample_theta / M_NEXT_PI;
                    
                    // Bilinear sampling from source
                    float src_x = sample_u * srcWidth - 0.5f;
                    float src_y = sample_v * srcHeight - 0.5f;
                    int src_x1 = std::clamp(static_cast<int>(std::floor(src_x)), 0, srcWidth - 1);
                    int src_y1 = std::clamp(static_cast<int>(std::floor(src_y)), 0, srcHeight - 1);
                    int src_x2 = std::clamp(src_x1 + 1, 0, srcWidth - 1);
                    int src_y2 = std::clamp(src_y1 + 1, 0, srcHeight - 1);
                    float wx = src_x - src_x1;
                    float wy = src_y - src_y1;
                    
                    // Sample the four pixels
                    int idx11 = (src_y1 * srcWidth + src_x1) * 4;
                    int idx12 = (src_y1 * srcWidth + src_x2) * 4;
                    int idx21 = (src_y2 * srcWidth + src_x1) * 4;
                    int idx22 = (src_y2 * srcWidth + src_x2) * 4;
                    
                    // Bilinear interpolation
                    float sr = (1-wx)*(1-wy)*srcPixels[idx11] + wx*(1-wy)*srcPixels[idx12] + 
                               (1-wx)*wy*srcPixels[idx21] + wx*wy*srcPixels[idx22];
                    float sg = (1-wx)*(1-wy)*srcPixels[idx11+1] + wx*(1-wy)*srcPixels[idx12+1] + 
                               (1-wx)*wy*srcPixels[idx21+1] + wx*wy*srcPixels[idx22+1];
                    float sb = (1-wx)*(1-wy)*srcPixels[idx11+2] + wx*(1-wy)*srcPixels[idx12+2] + 
                               (1-wx)*wy*srcPixels[idx21+2] + wx*wy*srcPixels[idx22+2];
                    float sa = (1-wx)*(1-wy)*srcPixels[idx11+3] + wx*(1-wy)*srcPixels[idx12+3] + 
                               (1-wx)*wy*srcPixels[idx21+3] + wx*wy*srcPixels[idx22+3];
                    
                    // Accumulate
                    float sampleWeight = 1.0f;  // Could be modified for importance sampling
                    r += sr * sampleWeight;
                    g += sg * sampleWeight;
                    b += sb * sampleWeight;
                    a += sa * sampleWeight;
                    weight += sampleWeight;
                }
                
                // Normalize
                if (weight > 0) {
                    r /= weight;
                    g /= weight;
                    b /= weight;
                    a /= weight;
                }
                
                // Write to destination
                int dstIdx = (y * dstWidth + x) * 4;
                dstPixels[dstIdx] = r;
                dstPixels[dstIdx+1] = g;
                dstPixels[dstIdx+2] = b;
                dstPixels[dstIdx+3] = a;
            }
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
        static const uint32_t k_bindless_texture_binding = 0;
        // The maximum number of bindless resources is limited by the device.
        static const uint32_t k_max_bindless_resources = 65535u;// moltenVK returns a invalid value. std::min(65535u, device.DeviceProperties().limits.maxPerStageDescriptorSamplers);

        // Create bindless descriptor pool
        VkDescriptorPoolSize pool_sizes_bindless[] =
        {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, k_max_bindless_resources}
        };

        // Update after bind is needed here, for each binding and in the descriptor set layout creation.
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = pool_sizes_bindless;
        poolInfo.maxSets = k_max_bindless_resources * 1;

        Vulkan::Check(vkCreateDescriptorPool(device.Handle(), &poolInfo, nullptr, &descriptorPool_),
                      "create global descriptor pool");

        // create set layout
        VkDescriptorBindingFlags bindless_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
            VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;

        VkDescriptorSetLayoutBinding vk_binding;
        vk_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        vk_binding.descriptorCount = k_max_bindless_resources;
        vk_binding.binding = k_bindless_texture_binding;

        vk_binding.stageFlags = VK_SHADER_STAGE_ALL;
        vk_binding.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo layout_info = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = 1;
        layout_info.pBindings = &vk_binding;
        layout_info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr
        };
        extended_info.bindingCount = 1;
        extended_info.pBindingFlags = &bindless_flags;

        layout_info.pNext = &extended_info;

        Vulkan::Check(vkCreateDescriptorSetLayout(device_.Handle(), &layout_info, nullptr, &layout_),
                      "create global descriptor set layout");

        VkDescriptorSetAllocateInfo alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc_info.descriptorPool = descriptorPool_;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &layout_;

        VkDescriptorSetVariableDescriptorCountAllocateInfoEXT count_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT
        };
        uint32_t max_binding = k_max_bindless_resources - 1;
        count_info.descriptorSetCount = 1;
        // This number is the max allocatable count
        count_info.pDescriptorCounts = &max_binding;
        alloc_info.pNext = &count_info;

        descriptorSets_.resize(1);

        Vulkan::Check(vkAllocateDescriptorSets(device_.Handle(), &alloc_info, descriptorSets_.data()),
                      "alloc global descriptor set");

        // for hdr to bind
        hdrSphericalHarmonics_.resize(100);
        
        GlobalTexturePool::instance_ = this;

        CreateDefaultTextures();
    }

    GlobalTexturePool::~GlobalTexturePool()
    {
        if (descriptorPool_ != nullptr)
        {
            vkDestroyDescriptorPool(device_.Handle(), descriptorPool_, nullptr);
            descriptorPool_ = nullptr;
        }

        if (layout_ != nullptr)
        {
            vkDestroyDescriptorSetLayout(device_.Handle(), layout_, nullptr);
            layout_ = nullptr;
        }

        defaultWhiteTexture_.reset();
        textureImages_.clear();
    }

    void GlobalTexturePool::BindTexture(uint32_t textureIdx, const TextureImage& textureImage)
    {
        VkDescriptorImageInfo imageInfo = {};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = textureImage.ImageView().Handle();
        imageInfo.sampler = textureImage.Sampler().Handle();


        VkWriteDescriptorSet descriptorWrite = {};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets_[0];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = textureIdx;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(
            device_.Handle(),
            1,
            &descriptorWrite, 0, nullptr);
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
                    if (hdr)
                    {
                        stbdata = reinterpret_cast<uint8_t*>(stbi_loadf_from_memory(copyedData, static_cast<uint32_t>(bytelength), &width, &height, &channels, STBI_rgb_alpha));
                        pixels = stbdata;
                        format = VK_FORMAT_R32G32B32A32_SFLOAT;
                        size = width * height * 4 * sizeof(float);

                        // Extract spherical harmonics from the base level
                        SphericalHarmonics sh = ProjectHDRToSH((float*)pixels, width, height);
                        hdrSphericalHarmonics_[newTextureIdx] = sh;

#if 0
                        // Calculate mipmap levels based on texture dimensions
                        int maxDimension = std::max(width, height);
                        int numMipLevels = static_cast<int>(std::floor(std::log2(maxDimension))) + 1;
                        miplevel = numMipLevels;
                        
                        // Original HDR data will be level 0
                        std::vector<float*> mipLevels(numMipLevels);
                        std::vector<int> mipWidths(numMipLevels);
                        std::vector<int> mipHeights(numMipLevels);

                        // Set base level
                        mipLevels[0] = reinterpret_cast<float*>(pixels);
                        mipWidths[0] = width;
                        mipHeights[0] = height;

                        // Generate roughness-filtered mipmaps
                        for (int level = 1; level < numMipLevels; ++level) {
                            // Calculate dimensions for this level
                            mipWidths[level] = std::max(mipWidths[level-1] / 2, 1);
                            mipHeights[level] = std::max(mipHeights[level-1] / 2, 1);
                            
                            // Allocate memory for this mip level
                            mipLevels[level] = new float[mipWidths[level] * mipHeights[level] * 4];
                            
                            // Calculate roughness for this level (increasing with mip level)
                            float roughness = static_cast<float>(level) / (numMipLevels - 1);
                            
                            // Process this mip level with roughness-based filtering
                            FilterEnvironmentMap(mipLevels[level-1], mipWidths[level-1], mipHeights[level-1], 
                                                 mipLevels[level], mipWidths[level], mipHeights[level], roughness);
                        }



                        // Create a texture with all mip levels
                        textureImages_[newTextureIdx] = std::make_unique<TextureImage>(
                            commandPool_, width, height, numMipLevels, format, pixels, size, mipLevels, mipWidths, mipHeights);

                        // Clean up mip levels (except level 0 which is handled by stbi_image_free)
                        for (int level = 1; level < numMipLevels; ++level) {
                            delete[] mipLevels[level];
                        }
#endif
                    }
                    else
                    {
                        // ldr texture, try cache fist
                        std::filesystem::path cachePath = Utilities::FileHelper::GetPlatformFilePath("cache");
                        std::filesystem::create_directories(cachePath);

                        // hash the texname
                        std::hash<std::string> hasher;
                        std::string hashname = fmt::format("{:x}", hasher(texname));
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
