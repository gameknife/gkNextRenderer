#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"

namespace Assets
{
	enum class ETextureStatus : uint8
	{
		ETS_Loaded,
		ETS_Unloaded,
	};

	enum class ETextureLifetime : uint8
	{
		ETL_Transient,
		ETL_Persistent,
	};

	struct FTextureBindingGroup
	{
		uint32_t GlobalIdx_;
		ETextureStatus Status_;
		ETextureLifetime Lifetime_ = ETextureLifetime::ETL_Transient;
	};
	
	class GlobalTexturePool final
	{
	public:
		GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& command_pool, Vulkan::CommandPool& command_pool_mt);
		~GlobalTexturePool();

		VkDescriptorSetLayout Layout() const { return descriptorSetManager_->DescriptorSetLayout().Handle(); }
		VkDescriptorSet DescriptorSet(uint32_t index) const { return descriptorSetManager_->DescriptorSets().Handle(0); }

		void BindTexture(uint32_t textureIdx, const TextureImage& textureImage);
		void BindStorageTexture(uint32_t textureIdx, const Vulkan::ImageView& textureImage);
		void BindShadowMap(uint32_t slot, const Vulkan::ImageView& view, const Vulkan::Sampler& sampler);
		uint32_t RegisterTexture(const std::string& textureName, std::unique_ptr<TextureImage> textureImage,
		                         ETextureLifetime lifetime = ETextureLifetime::ETL_Transient);
		uint32_t TryGetTexureIndex(const std::string& textureName) const;
		uint32_t RequestNewTextureFileAsync(const std::string& filename, bool hdr,
		                                    ETextureLifetime lifetime = ETextureLifetime::ETL_Transient);
		uint32_t RequestNewTextureMemAsync(const std::string& texname, const std::string& mime, bool hdr,
		                                   const unsigned char* data, size_t bytelength, bool srgb,
		                                   ETextureLifetime lifetime = ETextureLifetime::ETL_Transient);
		
		uint32_t TotalTextures() const {return static_cast<uint32_t>(textureImages_.size());}
		const std::unordered_map<std::string, FTextureBindingGroup>& TotalTextureMap() {return textureNameMap_;}

		void FreeTransientTextures();
		void CreateDefaultTextures();
		
		static GlobalTexturePool* GetInstance() {return instance_;}
		static uint32_t LoadTexture(const std::string& texname, const std::string& mime, const unsigned char* data,
		                            size_t bytelength, bool srgb);
		static uint32_t LoadTexture(const std::string& filename, bool srgb);
		static uint32_t LoadHDRTexture(const std::string& filename);

		static TextureImage* GetTextureImage(uint32_t idx);
		static TextureImage* GetTextureImageByName(const std::string& name);
		static uint32_t GetTextureIndexByName(const std::string& name);

		std::vector<SphericalHarmonics>& GetHDRSphericalHarmonics() { return hdrSphericalHarmonics_; }
		Vulkan::CommandPool& GetMainThreadCommandPool() { return mainThreadCommandPool_; }

		Vulkan::DescriptorSetManager& GetDescriptorManager() { return *descriptorSetManager_; }
	private:
		static GlobalTexturePool* instance_;

		const class Vulkan::Device& device_;
		Vulkan::CommandPool& commandPool_;
		Vulkan::CommandPool& mainThreadCommandPool_;
		bool textureWorkerUploadEnabled_ {};

		std::vector<std::unique_ptr<TextureImage>> textureImages_;
		std::unordered_map<std::string, FTextureBindingGroup> textureNameMap_;

		std::vector<SphericalHarmonics> hdrSphericalHarmonics_;

		std::unique_ptr<TextureImage> defaultWhiteTexture_;

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
	};

}
