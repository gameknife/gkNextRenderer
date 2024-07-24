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
		GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& command_pool);
		~GlobalTexturePool();

		VkDescriptorSetLayout Layout() const { return layout_; }
		VkDescriptorSet DescriptorSet(uint32_t index) const { return descriptorSets_[index]; }

		void BindTexture(uint32_t textureIdx, const TextureImage& textureImage);
		uint32_t TryGetTexureIndex(const std::string& textureName) const;
		uint32_t RequestNewTextureFileAsync(const std::string& filename, bool hdr);
		uint32_t RequestNewTextureMemAsync(const std::string& texname, bool hdr, const unsigned char* data, size_t bytelength);

		uint32_t TotalTextures() const {return static_cast<uint32_t>(textureImages_.size());}
		
		static GlobalTexturePool* GetInstance() {return instance_;}
		static uint32_t LoadTexture(const std::string& texname, const unsigned char* data, size_t bytelength, const Vulkan::SamplerConfig& samplerConfig);
		static uint32_t LoadTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig);
		static uint32_t LoadHDRTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig);

	private:
		static GlobalTexturePool* instance_;

		Vulkan::CommandPool& commandPool_;
		const class Vulkan::Device& device_;
		VkDescriptorPool descriptorPool_{};
		VkDescriptorSetLayout layout_{};
		std::vector<VkDescriptorSet> descriptorSets_;

		std::vector<std::unique_ptr<TextureImage>> textureImages_;
		std::unordered_map<std::string, uint32_t> textureNameMap_;
	};

}
