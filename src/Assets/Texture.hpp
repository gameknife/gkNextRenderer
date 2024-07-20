#pragma once

#include "Vulkan/Vulkan.hpp"
#include "Vulkan/Sampler.hpp"
#include <memory>
#include <string>
#include <vector>

namespace Vulkan {
	class CommandPool;
}

namespace Assets
{
	class TextureImage;
	
	class Texture final
	{
	public:

		static Texture LoadTexture(const std::string& texname, const unsigned char* data, size_t bytelength, const Vulkan::SamplerConfig& samplerConfig);
		static Texture LoadTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig);
		static Texture LoadHDRTexture(const std::string& filename, const Vulkan::SamplerConfig& samplerConfig);

		Texture& operator = (const Texture&) = delete;
		Texture& operator = (Texture&&) = delete;

		Texture() = delete;
		Texture(const Texture&) = delete;
		Texture(Texture&&) = default;
		~Texture() = default;

		const unsigned char* Pixels() const { return pixels_.get(); }
		int Width() const { return width_; }
		int Height() const { return height_; }
		bool Hdr() const {return hdr_ != 0; }
		int Channels() const { return channels_; }
		const std::string& Loadname() const { return loadname_; }

	private:

		Texture(std::string loadname, int width, int height, int channels, int hdr, unsigned char* pixels);

		Vulkan::SamplerConfig samplerConfig_;
		std::string loadname_;
		int width_;
		int height_;
		int channels_;
		int hdr_;
		std::unique_ptr<unsigned char, void (*) (void*)> pixels_;
	};

	class GlobalTexturePool final
	{
	public:
		GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& command_pool);
		~GlobalTexturePool();

		VkDescriptorSetLayout Layout() const { return layout_; }
		VkDescriptorSet DescriptorSet(uint32_t index) const { return descriptorSets_[index]; }

		void BindTexture(uint32_t textureIdx, const TextureImage& textureImage);
		void AddTexture(uint32_t textureIdx, const Texture& texture);
		
		static GlobalTexturePool* GetInstance() {return instance_;}

	private:
		static GlobalTexturePool* instance_;

		Vulkan::CommandPool& commandPool_;
		const class Vulkan::Device& device_;
		VkDescriptorPool descriptorPool_{};
		VkDescriptorSetLayout layout_{};
		std::vector<VkDescriptorSet> descriptorSets_;

		std::vector<std::unique_ptr<TextureImage>> textureImages_;
	};

}
