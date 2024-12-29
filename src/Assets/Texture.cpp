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
#include "Utilities/Console.hpp"
#include "Utilities/FileHelper.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/ImageView.hpp"

namespace Assets
{
    struct TextureTaskContext
    {
        int32_t textureId;
        TextureImage* transferPtr;
        float elapsed;
        std::array<char, 256> outputInfo;
    };
    
    uint32_t GlobalTexturePool::LoadTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig)
    {
        std::vector<uint8_t> data;
        Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(filename, data);
        return GetInstance()->RequestNewTextureMemAsync(filename, "texture/default", false, data.data(), data.size(), false);
    }

    uint32_t GlobalTexturePool::LoadTexture(const std::string& texname, const std::string& mime, const unsigned char* data, size_t bytelength, bool srgb)
    {
        return GetInstance()->RequestNewTextureMemAsync(texname, mime, false, data, bytelength, srgb);
    }

    uint32_t GlobalTexturePool::LoadHDRTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig)
    {
        std::vector<uint8_t> data;
        Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(filename, data);
        return GetInstance()->RequestNewTextureMemAsync(filename, "texture/default", true, data.data(), data.size(), false);
    }

    void GlobalTexturePool::UpdateHDRTexture(uint32_t idx, const std::string& filename, const Vulkan::SamplerConfig& samplerConfig)
    {
        GetInstance()->RequestUpdateTextureFileAsync(idx, filename, true);
    }

    TextureImage* GlobalTexturePool::GetTextureImage(uint32_t idx)
    {
        if(GetInstance()->textureImages_.size() > idx)
        {
            return GetInstance()->textureImages_[idx].get();
        }
        return nullptr;
    }

    TextureImage* GlobalTexturePool::GetTextureImageByName(const std::string& name)
    {
        uint32_t id = GetTextureIndexByName(name);
        if( id != -1 )
        {
            return  GetInstance()->textureImages_[id].get();
        }
        return nullptr;
    }

    uint32_t GlobalTexturePool::GetTextureIndexByName(const std::string& name)
    {
        if( GetInstance()->textureNameMap_.find(name) != GetInstance()->textureNameMap_.end() )
        {
            return GetInstance()->textureNameMap_[name];
        }
        return -1;
    }

    GlobalTexturePool::GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& command_pool, Vulkan::CommandPool& command_pool_mt) :
        device_(device),
        commandPool_(command_pool),
        mainThreadCommandPool_(command_pool_mt)
    {
        static const uint32_t k_bindless_texture_binding = 0;
        // The maximum number of bindless resources is limited by the device.
        static const uint32_t k_max_bindless_resources = std::min(65535u, device.DeviceProperties().limits.maxPerStageDescriptorSamplers);

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
        VkDescriptorBindingFlags bindless_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;

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

        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr};
        extended_info.bindingCount = 1;
        extended_info.pBindingFlags = &bindless_flags;

        layout_info.pNext = &extended_info;

        Vulkan::Check(vkCreateDescriptorSetLayout(device_.Handle(), &layout_info, nullptr, &layout_),
                      "create global descriptor set layout");

        VkDescriptorSetAllocateInfo alloc_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc_info.descriptorPool = descriptorPool_;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &layout_;

        VkDescriptorSetVariableDescriptorCountAllocateInfoEXT count_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT};
        uint32_t max_binding = k_max_bindless_resources - 1;
        count_info.descriptorSetCount = 1;
        // This number is the max allocatable count
        count_info.pDescriptorCounts = &max_binding;
        alloc_info.pNext = &count_info;

        descriptorSets_.resize(1);

        Vulkan::Check(vkAllocateDescriptorSets(device_.Handle(), &alloc_info, descriptorSets_.data()),
                      "alloc global descriptor set");

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
    
    void GlobalTexturePool::RequestUpdateTextureFileAsync(uint32_t textureIdx, const std::string& filename, bool hdr)
    {
        TaskCoordinator::GetInstance()->AddTask([this, filename, hdr, textureIdx](ResTask& task)
        {
            TextureTaskContext taskContext {};
            const auto timer = std::chrono::high_resolution_clock::now();
            
            // Load the texture in normal host memory.
            int width, height, channels;
            void* pixels = nullptr;
            uint32_t size = 0;
            VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
            
            if(hdr)
            {
                pixels = stbi_loadf(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
                size = width * height * 4 * sizeof(float);
            }
            else
            {
                pixels = stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
                format = VK_FORMAT_R8G8B8A8_UNORM;
                size = width * height * 4 * sizeof(uint8_t);
            }

            if (!pixels)
            {
                Throw(std::runtime_error("failed to load texture image '" + filename + "'"));
            }

            // thread reset may cause crash, created the new texture here, but reset in later main thread phase
            taskContext.transferPtr = new TextureImage(commandPool_, width, height, 1, format, static_cast<unsigned char*>((void*)pixels), size);
            stbi_image_free(pixels);
            taskContext.textureId = textureIdx;
            taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
            std::string info = fmt::format("reloaded {} ({} x {} x {}) in {:.2f}ms", filename, width, height, channels, taskContext.elapsed * 1000.f);
            std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
            task.SetContext( taskContext );
        }, [this](ResTask& task)
        {
            TextureTaskContext taskContext {};
            task.GetContext( taskContext );

            textureImages_[taskContext.textureId].reset(taskContext.transferPtr);
            BindTexture(taskContext.textureId, *(textureImages_[taskContext.textureId]));
            
            fmt::print("{}\n", taskContext.outputInfo.data());
        }, 0);
    }

    uint32_t GlobalTexturePool::RequestNewTextureMemAsync(const std::string& texname, const std::string& mime, bool hdr, const unsigned char* data, size_t bytelength, bool srgb)
    {
        if (textureNameMap_.find(texname) != textureNameMap_.end())
        {
            return textureNameMap_[texname];
        }

        textureImages_.emplace_back(nullptr);
        uint32_t newTextureIdx = static_cast<uint32_t>(textureImages_.size()) - 1;

        uint8_t* copyedData = new uint8_t[bytelength];
        memcpy(copyedData, data, bytelength);
        TaskCoordinator::GetInstance()->AddTask([this, hdr, srgb, texname, mime, copyedData, bytelength, newTextureIdx](ResTask& task)
        {
            TextureTaskContext taskContext {};
            const auto timer = std::chrono::high_resolution_clock::now();
            
            // Load the texture in normal host memory.
            int width, height, channels;
            uint8_t* pixels = nullptr;
            uint32_t size = 0;
            uint32_t miplevel = 1;
            VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

            ktxTexture2* kTexture;
            if ( mime.find("ktx") != std::string::npos )
            {
                ktx_error_code_e result = ktxTexture2_CreateFromMemory(copyedData, bytelength, KTX_TEXTURE_CREATE_CHECK_GLTF_BASISU_BIT, &kTexture);
                if (KTX_SUCCESS != result) {
                 Throw(std::runtime_error("failed to load ktx2 texture image "));
                }
                result = ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0);
                if (KTX_SUCCESS != result) {
                 Throw(std::runtime_error("failed to load ktx2 texture image "));
                }
                // ready to create compressed BC7?
                pixels = ktxTexture_GetData(ktxTexture(kTexture));
                //size = static_cast<uint32_t>( ktxTexture_GetDataSize(ktxTexture(kTexture)) );

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
                if(hdr)
                {
                    pixels = reinterpret_cast<uint8_t*>(stbi_loadf_from_memory(copyedData, static_cast<uint32_t>(bytelength), &width, &height, &channels, STBI_rgb_alpha));
                    format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    size = width * height * 4 * sizeof(float);
                }
                else
                {
                    pixels = stbi_load_from_memory(copyedData, static_cast<uint32_t>(bytelength), &width, &height, &channels, STBI_rgb_alpha);
                    format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                    size = width * height * 4 * sizeof(uint8_t);
                }
            }
            
            if (!pixels)
            {
                Throw(std::runtime_error("failed to load texture image "));
            }

            // create texture image
            textureImages_[newTextureIdx] = std::make_unique<TextureImage>(commandPool_, width, height, miplevel, format, pixels, size);
            BindTexture(newTextureIdx, *(textureImages_[newTextureIdx]));

            if ( mime.find("ktx") == std::string::npos )
            {
                stbi_image_free(pixels);
            }
            else
            {
                ktxTexture_Destroy(ktxTexture(kTexture));
            }

            taskContext.textureId = newTextureIdx;
            taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
            std::string info = fmt::format("loaded {} ({} x {} x {}) in {:.2f}ms", texname, width, height, miplevel, taskContext.elapsed * 1000.f);
            std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
            task.SetContext( taskContext );
        }, [this, copyedData](ResTask& task)
        {
            TextureTaskContext taskContext {};
            task.GetContext( taskContext );
            textureImages_[taskContext.textureId]->MainThreadPostLoading(mainThreadCommandPool_);
            if(!GOption->Benchmark) fmt::print("{}\n", taskContext.outputInfo.data());
            delete[] copyedData;
        }, 0);

        // cache in namemap
        textureNameMap_[texname] = newTextureIdx;
        return newTextureIdx;
    }

    GlobalTexturePool* GlobalTexturePool::instance_ = nullptr;
}
