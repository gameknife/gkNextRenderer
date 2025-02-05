#pragma once

#include "Vulkan/Vulkan.hpp"
#include "Vulkan/Sampler.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vulkan {
	class CommandPool;
}

namespace Assets
{
	class TextureImage;
	
	class GlobalTexturePool final
	{
	public:
		GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& command_pool, Vulkan::CommandPool& command_pool_mt);
		~GlobalTexturePool();

		VkDescriptorSetLayout Layout() const { return layout_; }
		VkDescriptorSet DescriptorSet(uint32_t index) const { return descriptorSets_[index]; }

		void BindTexture(uint32_t textureIdx, const TextureImage& textureImage);
		uint32_t TryGetTexureIndex(const std::string& textureName) const;
		uint32_t RequestNewTextureFileAsync(const std::string& filename, bool hdr);
		uint32_t RequestNewTextureMemAsync(const std::string& texname, const std::string& mime, bool hdr, const unsigned char* data, size_t bytelength, bool srgb);
		
		uint32_t TotalTextures() const {return static_cast<uint32_t>(textureImages_.size());}
		const std::unordered_map<std::string, uint32_t>& TotalTextureMap() {return textureNameMap_;}
		
		static GlobalTexturePool* GetInstance() {return instance_;}
		static uint32_t LoadTexture(const std::string& texname, const std::string& mime, const unsigned char* data, size_t bytelength, bool srgb);
		static uint32_t LoadTexture(const std::string& filename, bool srgb);
		static uint32_t LoadHDRTexture(const std::string& filename);

		static TextureImage* GetTextureImage(uint32_t idx);
		static TextureImage* GetTextureImageByName(const std::string& name);
		static uint32_t GetTextureIndexByName(const std::string& name);
	private:
		static GlobalTexturePool* instance_;

		const class Vulkan::Device& device_;
		Vulkan::CommandPool& commandPool_;
		Vulkan::CommandPool& mainThreadCommandPool_;
		
		VkDescriptorPool descriptorPool_{};
		VkDescriptorSetLayout layout_{};
		std::vector<VkDescriptorSet> descriptorSets_;

		std::vector<std::unique_ptr<TextureImage>> textureImages_;
		std::unordered_map<std::string, uint32_t> textureNameMap_;
	};

}
