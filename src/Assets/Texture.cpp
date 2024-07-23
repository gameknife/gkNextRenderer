#include "Texture.hpp"
#include "Utilities/StbImage.hpp"
#include "Utilities/Exception.hpp"
#include <chrono>
#include <iostream>

#include "TaskCoordinator.hpp"
#include "TextureImage.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/ImageView.hpp"

namespace Assets
{
    struct TextureTaskContext
    {
        float elapsed;
        std::array<char, 256> outputInfo;
    };

    
    uint32_t Texture::LoadTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig)
    {
        uint32_t texIdx = GlobalTexturePool::GetInstance()->TryGetTexureIndex(filename);
        if (texIdx != -1)
        {
            return texIdx;
        }
        // std::cout << "- loading '" << filename << "'... " << std::flush;
        // const auto timer = std::chrono::high_resolution_clock::now();
        //
        // // Load the texture in normal host memory.
        // int width, height, channels;
        // const auto pixels = stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        //
        // if (!pixels)
        // {
        // 	Throw(std::runtime_error("failed to load texture image '" + filename + "'"));
        // }
        //
        // const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
        // std::cout << "(" << width << " x " << height << " x " << channels << ") ";
        // std::cout << elapsed << "s" << std::endl;
        //
        // // bind texture to global texture pool directly
        // return GlobalTexturePool::GetInstance()->RequestNewTexture(Texture(filename, width, height, channels, 0, pixels));

        return GlobalTexturePool::GetInstance()->RequestNewTextureFileAsync(filename, false);
    }

    uint32_t Texture::LoadTexture(const std::string& texname, const unsigned char* data, size_t bytelength, const Vulkan::SamplerConfig& samplerConfig)
    {
        uint32_t texIdx = GlobalTexturePool::GetInstance()->TryGetTexureIndex(texname);
        if (texIdx != -1)
        {
            return texIdx;
        }
        // const auto timer = std::chrono::high_resolution_clock::now();
        //
        // // Load the texture in normal host memory.
        // int width, height, channels;
        // const auto pixels = stbi_load_from_memory(data, static_cast<uint32_t>(bytelength), &width, &height, &channels, STBI_rgb_alpha);
        //
        // if (!pixels)
        // {
        // 	Throw(std::runtime_error("failed to load texture image "));
        // }
        //
        // const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
        // std::cout << texname << "(" << width << " x " << height << " x " << channels << ") ";
        // std::cout << elapsed << "s" << std::endl;
        //
        // return GlobalTexturePool::GetInstance()->RequestNewTexture( Texture(texname, width, height, channels, 0, pixels));

        return GlobalTexturePool::GetInstance()->RequestNewTextureMemAsync(texname, false, data, bytelength);
    }

    uint32_t Texture::LoadHDRTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig)
    {
        uint32_t texIdx = GlobalTexturePool::GetInstance()->TryGetTexureIndex(filename);
        if (texIdx != -1)
        {
            return texIdx;
        }
        // std::cout << "- loading hdr '" << filename << "'... " << std::flush;
        // const auto timer = std::chrono::high_resolution_clock::now();
        //
        // // Load the texture in normal host memory.
        // int width, height, channels;
        // void* pixels = stbi_loadf(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        //
        // if (!pixels)
        // {
        // 	Throw(std::runtime_error("failed to load texture image '" + filename + "'"));
        // }
        //
        // const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
        // std::cout << "(" << width << " x " << height << " x " << channels << ") ";
        // std::cout << elapsed << "s" << std::endl;
        //
        // return GlobalTexturePool::GetInstance()->RequestNewTexture( Texture(filename, width, height, channels, 1, static_cast<unsigned char*>((void*)pixels)));

        return GlobalTexturePool::GetInstance()->RequestNewTextureFileAsync(filename, true);
    }

    Texture::Texture(std::string loadname, int width, int height, int channels, int hdr, unsigned char* const pixels) :
        loadname_(loadname),
        width_(width),
        height_(height),
        channels_(channels),
        hdr_(hdr),
        pixels_(pixels, stbi_image_free)
    {
    }

    GlobalTexturePool::GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& command_pool) :
        device_(device),
        commandPool_(command_pool)
    {
        static const uint32_t k_bindless_texture_binding = 0;
        static const uint32_t k_max_bindless_resources = 2048;

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

    uint32_t GlobalTexturePool::RequestNewTexture(const Texture& texture)
    {
        if (textureNameMap_.find(texture.Loadname()) != textureNameMap_.end())
        {
            return textureNameMap_[texture.Loadname()];
        }

        textureImages_.emplace_back(new TextureImage(commandPool_, texture));
        uint32_t newTextureIdx = static_cast<uint32_t>(textureImages_.size()) - 1;
        BindTexture(newTextureIdx, *(textureImages_.back()));

        // cache in namemap
        textureNameMap_[texture.Loadname()] = newTextureIdx;
        return newTextureIdx;
    }

    uint32_t GlobalTexturePool::RequestNewTextureFileAsync(const std::string& filename, bool hdr)
    {
        if (textureNameMap_.find(filename) != textureNameMap_.end())
        {
            return textureNameMap_[filename];
        }

        textureImages_.emplace_back(nullptr);
        uint32_t newTextureIdx = static_cast<uint32_t>(textureImages_.size()) - 1;
        
        TaskCoordinator::GetInstance()->AddTask([this, filename, hdr, newTextureIdx](ResTask& task)
        {
            TextureTaskContext taskContext {};
            const auto timer = std::chrono::high_resolution_clock::now();
            
            // Load the texture in normal host memory.
            int width, height, channels;
            void* pixels = nullptr;
            if(hdr)
            {
                pixels = stbi_loadf(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            }
            else
            {
                pixels = stbi_load(filename.c_str(), &width, &height, &channels, STBI_rgb_alpha);
            }

            if (!pixels)
            {
                Throw(std::runtime_error("failed to load texture image '" + filename + "'"));
            }

            textureImages_[newTextureIdx] = std::make_unique<TextureImage>(commandPool_, Texture(filename, width, height, channels, hdr, static_cast<unsigned char*>((void*)pixels)));
            BindTexture(newTextureIdx, *(textureImages_[newTextureIdx]));

            taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
            std::stringstream stream;
            stream << "loaded " << filename <<  "(" << width << " x " << height << " x " << channels << ") in " << std::fixed << std::setprecision(2) << taskContext.elapsed * 1000.f << "ms";
            std::string info = stream.str();
            std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
            task.SetContext( taskContext );
        }, [](ResTask& task)
        {
            TextureTaskContext taskContext {};
            task.GetContext( taskContext );
            std::cout << taskContext.outputInfo.data() << std::endl;
        }, 0);

        // cache in namemap
        textureNameMap_[filename] = newTextureIdx;
        return newTextureIdx;
    }

    uint32_t GlobalTexturePool::RequestNewTextureMemAsync(const std::string& texname, bool hdr, const unsigned char* data, size_t bytelength)
    {
        if (textureNameMap_.find(texname) != textureNameMap_.end())
        {
            return textureNameMap_[texname];
        }

        textureImages_.emplace_back(nullptr);
        uint32_t newTextureIdx = static_cast<uint32_t>(textureImages_.size()) - 1;

        uint8_t* copyedData = new uint8_t[bytelength];
        memcpy(copyedData, data, bytelength);
        TaskCoordinator::GetInstance()->AddTask([this, texname, copyedData, bytelength, newTextureIdx](ResTask& task)
        {
            TextureTaskContext taskContext {};
            const auto timer = std::chrono::high_resolution_clock::now();
            
            // Load the texture in normal host memory.
            int width, height, channels;
            const auto pixels = stbi_load_from_memory(copyedData, static_cast<uint32_t>(bytelength), &width, &height, &channels, STBI_rgb_alpha);

            if (!pixels)
            {
                Throw(std::runtime_error("failed to load texture image "));
            }

            // create texture image
            textureImages_[newTextureIdx] = std::make_unique<TextureImage>(commandPool_, Texture(texname, width, height, channels, 0, pixels));
            BindTexture(newTextureIdx, *(textureImages_[newTextureIdx]));

            taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();
            std::stringstream stream;
            stream << "loaded " << texname <<  "(" << width << " x " << height << " x " << channels << ") in " << std::fixed << std::setprecision(2) << taskContext.elapsed * 1000.f << "ms";
            std::string info = stream.str();
            std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
            task.SetContext( taskContext );
        }, [copyedData](ResTask& task)
        {
            TextureTaskContext taskContext {};
            task.GetContext( taskContext );
            std::cout << taskContext.outputInfo.data() << std::endl;
            delete[] copyedData;
        }, 0);

        // cache in namemap
        textureNameMap_[texname] = newTextureIdx;
        return newTextureIdx;
    }

    GlobalTexturePool* GlobalTexturePool::instance_ = nullptr;
}
