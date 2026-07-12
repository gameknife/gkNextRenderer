#pragma once

#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
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

	struct FTextureCpuSource
	{
		std::string TextureName;
		std::string Mime;
		std::vector<uint8_t> Bytes;
		bool Srgb = false;
		bool Hdr = false;
	};
	
	class GlobalTexturePool final
	{
	public:
		// Capacity of the bindless descriptor arrays (sample/storage). Slots above the registered
		// texture count are valid targets for explicitly-bound render outputs (see BindSampleTexture).
		static constexpr uint32_t kMaxBindlessSlots = 65535u;

		enum class EHDRTextureResidency : uint8
		{
			LowestMip,
			FullMip,
		};

		GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& command_pool, Vulkan::CommandPool& command_pool_mt);
		~GlobalTexturePool();

		VkDescriptorSetLayout Layout() const { return descriptorSetManager_->DescriptorSetLayout().Handle(); }
		VkDescriptorSet DescriptorSet(uint32_t index) const { return descriptorSetManager_->DescriptorSets().Handle(0); }

		void BindTexture(uint32_t textureIdx, const TextureImage& textureImage);
		// Bind an arbitrary sampled image view (e.g. an offscreen RenderView output) into the
		// sample-texture array (set0,binding0) so it can be shown via ImGui::Image. Caller keeps the
		// image in SHADER_READ_ONLY_OPTIMAL when sampled.
		void BindSampleTexture(uint32_t textureIdx, const Vulkan::ImageView& view, const Vulkan::Sampler& sampler);
		void BindStorageTexture(uint32_t textureIdx, const Vulkan::ImageView& textureImage);
		void BindShadowMap(uint32_t slot, const Vulkan::ImageView& view, const Vulkan::Sampler& sampler);
		uint32_t RegisterTexture(const std::string& textureName, std::unique_ptr<TextureImage> textureImage,
		                         ETextureLifetime lifetime = ETextureLifetime::ETL_Transient);
		void ReleaseTexture(uint32_t textureIdx);
		uint32_t TryGetTextureIndex(const std::string& textureName) const;
		uint32_t RequestNewTextureFileAsync(const std::string& filename, bool hdr,
		                                    ETextureLifetime lifetime = ETextureLifetime::ETL_Transient);
		uint32_t RequestNewTextureMemAsync(const std::string& texname, const std::string& mime, bool hdr,
		                                   const unsigned char* data, size_t bytelength, bool srgb,
		                                   ETextureLifetime lifetime = ETextureLifetime::ETL_Transient);
		
		uint32_t TotalTextures() const {return static_cast<uint32_t>(textureImages_.size());}
		const std::unordered_map<std::string, FTextureBindingGroup>& TotalTextureMap() {return textureNameMap_;}

		void FreeTransientTextures();
		void CreateDefaultTextures();
		void TickHDRTextureResidency(uint32_t activeTextureIdx, bool hasSky, uint32_t frameIndex, bool streamingEnabled);
		
		static GlobalTexturePool* GetInstance() {return instance_;}
		static uint32_t LoadTexture(const std::string& texname, const std::string& mime, const unsigned char* data,
		                            size_t bytelength, bool srgb);
		static uint32_t LoadTexture(const std::string& filename, bool srgb);
		static uint32_t LoadHDRTexture(const std::string& filename);

		static TextureImage* GetTextureImage(uint32_t idx);
		static TextureImage* GetTextureImageByName(const std::string& name);
		static uint32_t GetTextureIndexByName(const std::string& name);
		static const FTextureCpuSource* GetTextureCpuSource(uint32_t idx);

		std::vector<SphericalHarmonics>& GetHDRSphericalHarmonics() { return hdrSphericalHarmonics_; }
		const FTextureCpuSource* GetCpuSource(uint32_t textureIdx) const;
		Vulkan::CommandPool& GetMainThreadCommandPool() { return mainThreadCommandPool_; }

		Vulkan::DescriptorSetManager& GetDescriptorManager() { return *descriptorSetManager_; }
		// Engine integration hooks (Assets must not depend on Runtime):
		// streaming policy decides whether HDR textures stream in at lowest mip;
		// the SH-updated callback fires after an HDR texture (re)load.
		void SetHdrStreamingPolicy(std::function<bool()> policy) { hdrStreamingPolicy_ = std::move(policy); }
		void SetHdrShUpdatedCallback(std::function<void()> callback) { hdrShUpdatedCallback_ = std::move(callback); }

	private:
		std::function<bool()> hdrStreamingPolicy_;
		std::function<void()> hdrShUpdatedCallback_;

		struct FHDRTextureResidencyState
		{
			std::string TextureName;
			ETextureLifetime Lifetime = ETextureLifetime::ETL_Persistent;
			EHDRTextureResidency Current = EHDRTextureResidency::LowestMip;
			EHDRTextureResidency Target = EHDRTextureResidency::LowestMip;
			uint32_t LastTouchedFrame = 0;
			uint32_t DemandFrames = 0;
			bool IsHDR = false;
			bool Pending = false;
		};

		void QueueHDRTextureResidency(uint32_t textureIdx, EHDRTextureResidency targetResidency);

		static GlobalTexturePool* instance_;

		const class Vulkan::Device& device_;
		Vulkan::CommandPool& commandPool_;
		Vulkan::CommandPool& mainThreadCommandPool_;
		bool textureWorkerUploadEnabled_ {};

		std::vector<std::unique_ptr<TextureImage>> textureImages_;
		std::unordered_map<std::string, FTextureBindingGroup> textureNameMap_;
		std::vector<FTextureCpuSource> textureCpuSources_;

		std::vector<SphericalHarmonics> hdrSphericalHarmonics_;
		std::vector<FHDRTextureResidencyState> hdrTextureResidency_;

		std::unique_ptr<TextureImage> defaultWhiteTexture_;

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
	};

}
