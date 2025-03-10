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
            return GetInstance()->textureNameMap_[name];
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
            return textureNameMap_.at(textureName);
        }
        return -1;
    }

    uint32_t GlobalTexturePool::RequestNewTextureMemAsync(const std::string& texname, const std::string& mime, bool hdr,
                                                          const unsigned char* data, size_t bytelength, bool srgb)
    {
        if (textureNameMap_.find(texname) != textureNameMap_.end())
        {
            return textureNameMap_[texname];
        }

        textureImages_.emplace_back(nullptr);
        uint32_t newTextureIdx = static_cast<uint32_t>(textureImages_.size()) - 1;

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
                        // ktx now don't support hdr, use stbi import
                        stbdata = reinterpret_cast<uint8_t*>(stbi_loadf_from_memory(copyedData, static_cast<uint32_t>(bytelength), &width, &height, &channels, STBI_rgb_alpha));
                        pixels = stbdata;
                        format = VK_FORMAT_R32G32B32A32_SFLOAT;
                        size = width * height * 4 * sizeof(float);

                        SphericalHarmonics sh = ProjectHDRToSH((float*)pixels, width, height);
                        hdrSphericalHarmonics_[newTextureIdx] = sh;
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
                textureImages_[newTextureIdx] = std::make_unique<TextureImage>(
                    commandPool_, width, height, miplevel, format, pixels, size);
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

        // cache in namemap
        textureNameMap_[texname] = newTextureIdx;
        return newTextureIdx;
    }

    GlobalTexturePool* GlobalTexturePool::instance_ = nullptr;
}
