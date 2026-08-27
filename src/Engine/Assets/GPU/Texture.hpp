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
    
    // How many descriptors the bindless arrays declare. The full profile mirrors the slot registry
    // in assets/shaders/common/BindlessTexture.slang; a device that cannot back arrays that large
    // (MoltenVK on A12X-class GPUs) gets the compatibility profile instead.
    //
    // One struct, three readers: the descriptor layout GlobalTexturePool builds, the scene-texture
    // ceiling in RegisterTexture, and the device-capability probe in VulkanBaseRenderer. They have
    // to agree -- if the ceiling outgrows the array, RegisterTexture writes past the end of its
    // binding, and if the probe undercounts, a layout the device cannot create passes the check.
    struct FBindlessProfile
    {
        uint32_t sampledTextureSlots = 0;
        uint32_t storageTextureSlots = 0;
        uint32_t shadowMapSlots = 0;
        uint32_t volumeSlots = 0;
        // Scene textures may only take indices below this. The rest of the sampled array is handed
        // out explicitly (thumbnails, offscreen view outputs), so overrunning it would silently
        // overwrite those descriptors.
        uint32_t sceneTextureCapacity = 0;
        // Whether any shader in this profile samples the scene-texture array. The compatibility
        // renderer shades from material constants alone, so its textures are still loaded and
        // uploaded (materials keep meaningful ids for later) but never bound -- which is why its
        // tiny array is not a content limit there, and why exhausting it is not an error.
        bool bindsSceneTextures = true;

        // What vkCreateDescriptorSetLayout charges against the per-stage limits. A
        // COMBINED_IMAGE_SAMPLER element costs one sampled image *and* one sampler, so the shadow,
        // sample and volume-sample arrays land in both totals.
        constexpr uint32_t CombinedImageSamplers() const
        {
            return shadowMapSlots + sampledTextureSlots + volumeSlots;
        }
        constexpr uint32_t StorageImages() const { return storageTextureSlots + volumeSlots; }

        constexpr bool operator==(const FBindlessProfile&) const = default;

        static constexpr FBindlessProfile Full();
        static constexpr FBindlessProfile Compatibility();
    };

    constexpr FBindlessProfile FBindlessProfile::Full()
    {
        return {
            .sampledTextureSlots = static_cast<uint32_t>(Bindless::RES_SLOT_COUNT),
            .storageTextureSlots = static_cast<uint32_t>(Bindless::RES_SLOT_COUNT),
            .shadowMapSlots = 16,
            .volumeSlots = static_cast<uint32_t>(Bindless::RES_VOLUME_COUNT),
            .sceneTextureCapacity = static_cast<uint32_t>(Bindless::RES_SCENE_TEXTURE_CAPACITY),
            .bindsSceneTextures = true,
        };
    }

    // Sized against the tightest limit on the target class of device: MoltenVK on an A12X reports
    // maxPerStageDescriptorSamplers = 16. Shadow maps, volumes and storage images all belong to
    // passes the compatibility renderer never runs, so they are zero.
    //
    // The sampled array is small on purpose and is NOT a cap on scene content: nothing in this
    // profile samples it, so a scene with more textures than slots simply leaves the extras
    // unbound. Raise a field here (and re-run the probe) before adding a compatibility pass that
    // actually reads a texture.
    constexpr FBindlessProfile FBindlessProfile::Compatibility()
    {
        return {
            .sampledTextureSlots = 8,
            .storageTextureSlots = 0,
            .shadowMapSlots = 0,
            .volumeSlots = 0,
            .sceneTextureCapacity = 8,
            .bindsSceneTextures = false,
        };
    }

    class GlobalTexturePool final
    {
    public:
        // Capacity of the 2D bindless descriptor arrays (sample/storage), and the ceiling on how
        // many scene textures may be registered before they would collide with the explicitly-bound
        // region. Both come from the slot registry in assets/shaders/common/BindlessTexture.slang;
        // see the address-space comment there before changing either.
        static constexpr uint32_t kMaxBindlessSlots = static_cast<uint32_t>(Bindless::RES_SLOT_COUNT);
        static constexpr uint32_t kMaxSceneTextures = static_cast<uint32_t>(Bindless::RES_SCENE_TEXTURE_CAPACITY);
        static constexpr uint32_t kMaxVolumeBindlessSlots = static_cast<uint32_t>(Bindless::RES_VOLUME_COUNT);

        enum class EHDRTextureResidency : uint8
        {
            LowestMip,
            FullMip,
        };

        GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& command_pool,
                          Vulkan::CommandPool& command_pool_mt,
                          const FBindlessProfile& profile = FBindlessProfile::Full(),
                          bool supportsBCTextures = true);
        ~GlobalTexturePool();

        const FBindlessProfile& Profile() const { return profile_; }

        VkDescriptorSetLayout Layout() const { return descriptorSetManager_->DescriptorSetLayout().Handle(); }
        VkDescriptorSet DescriptorSet(uint32_t index) const { return descriptorSetManager_->DescriptorSets().Handle(0); }

        void BindTexture(uint32_t textureIdx, const TextureImage& textureImage);
        // Bind an arbitrary sampled image view (e.g. an offscreen RenderView output) into the
        // sample-texture array (set0,binding0) so it can be shown via ImGui::Image. Caller keeps the
        // image in SHADER_READ_ONLY_OPTIMAL when sampled.
        void BindSampleTexture(uint32_t textureIdx, const Vulkan::ImageView& view, const Vulkan::Sampler& sampler);
        // Replace an explicitly-bound sampled slot with the persistent fallback before the
        // original image view is destroyed.
        void BindDefaultSampleTexture(uint32_t textureIdx);
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
        static uint32_t LoadTexture(const std::string& filename, bool srgb, ETextureLifetime lifetime);
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

        enum class EBindingArray : uint8
        {
            SampleTexture,
            StorageTexture,
            ShadowMap,
            VolumeSample,
            VolumeStorage,
            Count,
        };
        bool CanBindSlot(uint32_t slot, uint32_t declaredCount, EBindingArray array) const;

        // Texture uploads finish on task-coordinator workers, so the warn-once state is atomic.
        mutable std::array<std::atomic_flag, static_cast<size_t>(EBindingArray::Count)> overflowReported_{};

        static GlobalTexturePool* instance_;

        const class Vulkan::Device& device_;
        Vulkan::CommandPool& commandPool_;
        Vulkan::CommandPool& mainThreadCommandPool_;
        bool textureWorkerUploadEnabled_ {};
        FBindlessProfile profile_ = FBindlessProfile::Full();
        // KTX2/Basis assets transcode to BC7 where the device can sample it and to plain RGBA
        // otherwise. Mobile and Apple GPUs take the second path.
        bool supportsBCTextures_ = true;

        std::vector<std::unique_ptr<TextureImage>> textureImages_;
        std::unordered_map<std::string, FTextureBindingGroup> textureNameMap_;
        std::vector<FTextureCpuSource> textureCpuSources_;

        std::vector<SphericalHarmonics> hdrSphericalHarmonics_;
        std::vector<FHDRTextureResidencyState> hdrTextureResidency_;

        std::unique_ptr<TextureImage> defaultWhiteTexture_;

        std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
    };

}
