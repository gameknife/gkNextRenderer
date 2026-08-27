#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Utilities/StbImage.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Common/CoreMinimal.hpp"

#include "Engine/Options.hpp"
#include "Engine/Runtime/Subsystems/TaskCoordinator.hpp"
#include "Engine/Assets/GPU/TextureImage.hpp"
#include "Engine/Assets/GPU/HdrTextureCache.hpp"
#include "Engine/Utilities/FileHelper.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "ThirdParty/lzav/lzav.h"

#include <limits>
#include <system_error>
#include <webp/decode.h>

#include <ktx.h>

#define M_NEXT_PI 3.14159265358979323846f

namespace
{
    constexpr uint32_t kSampleTextureBinding = 0;
    constexpr uint32_t kStorageTextureBinding = 1;
    constexpr uint32_t kShadowMapBinding = 2;
    constexpr uint32_t kVolumeSampleTextureBinding = 3;
    constexpr uint32_t kVolumeStorageTextureBinding = 4;

    bool IsVolumeImage(const Vulkan::ImageView& view)
    {
        return view.ViewType() == VK_IMAGE_VIEW_TYPE_3D;
    }

    uint32_t VolumeDescriptorIndex(uint32_t bindlessIdx)
    {
        const uint32_t volumeBase = static_cast<uint32_t>(Assets::Bindless::RES_VOLUME_BASE);
        const uint32_t volumeEnd = volumeBase + Assets::GlobalTexturePool::kMaxVolumeBindlessSlots;
        if (bindlessIdx < volumeBase || bindlessIdx >= volumeEnd)
        {
            Throw(std::invalid_argument(fmt::format(
                "3D bindless slot {} is outside the volume range [{}..{})",
                bindlessIdx, volumeBase, volumeEnd)));
        }
        return bindlessIdx - volumeBase;
    }

    bool ShouldEnableTextureWorkerUpload(const Vulkan::Device& device)
    {
        if (device.TransferFamilyIndex() == static_cast<int32_t>(device.GraphicsFamilyIndex()))
        {
            return false;
        }

        const bool validationEnabled = GOption && GOption->Validation;
        return !validationEnabled;
    }
}

namespace Assets
{
    struct TextureTaskContext
    {
        int32_t textureId;
        TextureImage* transferPtr;
        float elapsed;
        bool needFlushHDRSH;
        uint8_t hdrResidency;
        std::array<char, 256> outputInfo;
    };
    
    uint32_t GlobalTexturePool::LoadTexture(const std::string& filename, bool srgb)
    {
        return LoadTexture(filename, srgb, ETextureLifetime::ETL_Transient);
    }

    uint32_t GlobalTexturePool::LoadTexture(const std::string& filename, bool srgb, ETextureLifetime lifetime)
    {
        auto& pakSystem = Utilities::Package::FPackageFileSystem::GetInstance();
        if (!Utilities::FileHelper::IsAssetAvailable(filename))
        {
            SPDLOG_WARN("Texture '{}' is unavailable; using a placeholder texture.", filename);
            return GetInstance()->RequestNewTextureMemAsync(
                filename, "image/png", false, nullptr, 0, srgb, ETextureLifetime::ETL_Transient);
        }

        std::vector<uint8_t> data;
        if (!pakSystem.LoadFile(filename, data) || data.empty())
        {
            SPDLOG_WARN("Texture '{}' failed to load; using a placeholder texture.", filename);
            return GetInstance()->RequestNewTextureMemAsync(
                filename, "image/png", false, nullptr, 0, srgb, ETextureLifetime::ETL_Transient);
        }
        std::filesystem::path path(filename);
        std::string mime = std::string("image/") + path.extension().string().substr(1);
        return GetInstance()->RequestNewTextureMemAsync(
            filename, mime, false, data.data(), data.size(), srgb, lifetime);
    }

    uint32_t GlobalTexturePool::LoadTexture(const std::string& texname, const std::string& mime,
                                            const unsigned char* data, size_t bytelength, bool srgb)
    {
        return GetInstance()->RequestNewTextureMemAsync(
            texname, mime, false, data, bytelength, srgb, ETextureLifetime::ETL_Transient);
    }

    uint32_t GlobalTexturePool::LoadHDRTexture(const std::string& filename)
    {
        auto& pakSystem = Utilities::Package::FPackageFileSystem::GetInstance();
        if (!Utilities::FileHelper::IsAssetAvailable(filename))
        {
            SPDLOG_WARN("HDR texture '{}' is unavailable; using a placeholder environment.", filename);
            return GetInstance()->RequestNewTextureMemAsync(
                filename, "image/hdr", true, nullptr, 0, false, ETextureLifetime::ETL_Persistent);
        }

        std::vector<uint8_t> data;
        const bool loaded = pakSystem.LoadFile(filename, data);
        if (!loaded || data.empty())
        {
            SPDLOG_WARN("HDR texture '{}' is unavailable; using a placeholder environment.", filename);
            return GetInstance()->RequestNewTextureMemAsync(
                filename, "image/hdr", true, nullptr, 0, false, ETextureLifetime::ETL_Persistent);
        }

        return GetInstance()->RequestNewTextureMemAsync(
            filename, "image/hdr", true, data.data(), data.size(), false, ETextureLifetime::ETL_Persistent);
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

    const FTextureCpuSource* GlobalTexturePool::GetTextureCpuSource(uint32_t idx)
    {
        if (!GetInstance())
        {
            return nullptr;
        }
        return GetInstance()->GetCpuSource(idx);
    }

    const FTextureCpuSource* GlobalTexturePool::GetCpuSource(uint32_t textureIdx) const
    {
        if (textureIdx >= textureCpuSources_.size() || textureCpuSources_[textureIdx].Bytes.empty())
        {
            return nullptr;
        }
        return &textureCpuSources_[textureIdx];
    }

    GlobalTexturePool::GlobalTexturePool(const Vulkan::Device& device, Vulkan::CommandPool& commandPool,
                                         Vulkan::CommandPool& commandPoolMt, const FBindlessProfile& profile,
                                         const bool supportsBCTextures) :
        device_(device),
        commandPool_(commandPool),
        mainThreadCommandPool_(commandPoolMt),
        textureWorkerUploadEnabled_(ShouldEnableTextureWorkerUpload(device)),
        profile_(profile),
        supportsBCTextures_(supportsBCTextures)
    {
        if (!textureWorkerUploadEnabled_)
        {
            SPDLOG_INFO("Texture uploads will run on the main thread because no dedicated transfer queue is available or validation mode is active");
        }

        // Sized from the profile rather than the raw device maximum: the arrays are allocated at
        // their full declared count, so declaring 65535 would burn ~4 MB of descriptor pool for
        // slots nothing can address. moltenVK also reports an unusable
        // limits.maxPerStageDescriptorSamplers, which is why this is not derived from the device.
        //
        // A zero-count array is dropped rather than declared: VkDescriptorPoolSize::descriptorCount
        // must be greater than zero, and a pass that needs the binding should fail loudly at
        // pipeline creation instead of silently binding into an array that does not exist.
        std::vector<Vulkan::DescriptorBinding> descriptorBindings;
        const auto addBinding = [&descriptorBindings](uint32_t binding, uint32_t count, VkDescriptorType type)
        {
            if (count > 0)
            {
                descriptorBindings.push_back({binding, count, type, VK_SHADER_STAGE_ALL});
            }
        };
        addBinding(kShadowMapBinding, profile_.shadowMapSlots, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        addBinding(kSampleTextureBinding, profile_.sampledTextureSlots, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        addBinding(kStorageTextureBinding, profile_.storageTextureSlots, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        addBinding(kVolumeSampleTextureBinding, profile_.volumeSlots, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        addBinding(kVolumeStorageTextureBinding, profile_.volumeSlots, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
        SPDLOG_INFO("Bindless descriptor layout: sampled {}, storage {}, shadow {}, volume {} "
                    "(per-stage cost: {} combined image samplers, {} storage images); BC textures {}",
                    profile_.sampledTextureSlots, profile_.storageTextureSlots, profile_.shadowMapSlots,
                    profile_.volumeSlots, profile_.CombinedImageSamplers(), profile_.StorageImages(),
                    supportsBCTextures_ ? "supported" : "unsupported (transcoding to RGBA)");
        descriptorSetManager_.reset(new Vulkan::DescriptorSetManager(device, descriptorBindings, 1, true));

        // for hdr to bind
        hdrSphericalHarmonics_.resize(100);

        GlobalTexturePool::instance_ = this;

        CreateDefaultTextures();
    }

    GlobalTexturePool::~GlobalTexturePool()
    {
        defaultWhiteTexture_.reset();
        textureImages_.clear();
        descriptorSetManager_.reset();
    }

    void GlobalTexturePool::BindTexture(uint32_t textureIdx, const TextureImage& textureImage)
    {
        BindSampleTexture(textureIdx, textureImage.ImageView(), textureImage.Sampler());
    }

    // Writing a descriptor past the end of its binding is undefined behaviour, not a clamped write:
    // it corrupts whatever the driver has after the array. Every array here is sized by the active
    // profile, so this is the last line of defence for all of them.
    bool GlobalTexturePool::CanBindSlot(const uint32_t slot, const uint32_t declaredCount,
                                        const EBindingArray array) const
    {
        if (slot < declaredCount)
        {
            return true;
        }
        static constexpr std::array<const char*, static_cast<size_t>(EBindingArray::Count)> names{
            "sample texture", "storage texture", "shadow map", "volume sample", "volume storage",
        };
        const auto arrayIndex = static_cast<size_t>(array);
        if (!overflowReported_[arrayIndex].test_and_set(std::memory_order_relaxed))
        {
            SPDLOG_WARN("Bindless '{}' array holds {} descriptors; slot {} and any later one are not "
                        "bound under this profile.", names[arrayIndex], declaredCount, slot);
        }
        return false;
    }

    void GlobalTexturePool::BindSampleTexture(uint32_t textureIdx, const Vulkan::ImageView& view,
                                              const Vulkan::Sampler& sampler)
    {
        const bool isVolume = IsVolumeImage(view);
        if (!CanBindSlot(isVolume ? VolumeDescriptorIndex(textureIdx) : textureIdx,
                         isVolume ? profile_.volumeSlots : profile_.sampledTextureSlots,
                         isVolume ? EBindingArray::VolumeSample : EBindingArray::SampleTexture))
        {
            return;
        }

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();
        const VkDescriptorImageInfo imageInfo{
            sampler.Handle(),
            view.Handle(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        std::vector<VkWriteDescriptorSet> descriptorWrites =
        {
            descriptorSets.Bind(0, isVolume ? kVolumeSampleTextureBinding : kSampleTextureBinding,
                                imageInfo, isVolume ? VolumeDescriptorIndex(textureIdx) : textureIdx, 1),
        };
        descriptorSets.UpdateDescriptors(0, descriptorWrites);
    }

    void GlobalTexturePool::BindDefaultSampleTexture(const uint32_t textureIdx)
    {
        if (defaultWhiteTexture_)
        {
            BindTexture(textureIdx, *defaultWhiteTexture_);
        }
    }

    void GlobalTexturePool::BindStorageTexture(uint32_t textureIdx, const Vulkan::ImageView& textureImage)
    {
        const bool isVolume = IsVolumeImage(textureImage);
        if (!CanBindSlot(isVolume ? VolumeDescriptorIndex(textureIdx) : textureIdx,
                         isVolume ? profile_.volumeSlots : profile_.storageTextureSlots,
                         isVolume ? EBindingArray::VolumeStorage : EBindingArray::StorageTexture))
        {
            return;
        }

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();
        const VkDescriptorImageInfo imageInfo{
            VK_NULL_HANDLE,
            textureImage.Handle(),
            VK_IMAGE_LAYOUT_GENERAL,
        };
        std::vector<VkWriteDescriptorSet> descriptorWrites =
        {
            descriptorSets.Bind(0, isVolume ? kVolumeStorageTextureBinding : kStorageTextureBinding,
                                imageInfo, isVolume ? VolumeDescriptorIndex(textureIdx) : textureIdx, 1),
        };
        descriptorSets.UpdateDescriptors(0, descriptorWrites);
    }

    void GlobalTexturePool::BindShadowMap(uint32_t slot, const Vulkan::ImageView& view, const Vulkan::Sampler& sampler)
    {
        if (!CanBindSlot(slot, profile_.shadowMapSlots, EBindingArray::ShadowMap))
        {
            return;
        }

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();
        const VkDescriptorImageInfo imageInfo{
            sampler.Handle(),
            view.Handle(),
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        };
        std::vector<VkWriteDescriptorSet> descriptorWrites =
        {
            descriptorSets.Bind(0, kShadowMapBinding, imageInfo, slot, 1),
        };
        descriptorSets.UpdateDescriptors(0, descriptorWrites);
    }

    uint32_t GlobalTexturePool::RegisterTexture(const std::string& textureName, std::unique_ptr<TextureImage> textureImage,
                                                ETextureLifetime lifetime)
    {
        if (!textureImage)
        {
            return static_cast<uint32_t>(-1);
        }

        textureImage->SetDebugName(fmt::format("Texture {}", textureName));

        uint32_t textureIdx = 0;
        auto textureIt = textureNameMap_.find(textureName);
        if (textureIt != textureNameMap_.end())
        {
            textureIdx = textureIt->second.GlobalIdx_;
            textureIt->second.Status_ = ETextureStatus::ETS_Loaded;
            textureIt->second.Lifetime_ = lifetime;
            if (textureImages_.size() <= textureIdx)
            {
                textureImages_.resize(static_cast<size_t>(textureIdx) + 1);
            }
            textureImages_[textureIdx] = std::move(textureImage);
        }
        else
        {
            textureIdx = static_cast<uint32_t>(textureImages_.size());
            // Past this point the texture index would address the explicitly-bound region
            // (thumbnails, view outputs, volumes) and silently overwrite those descriptors -- or,
            // under a constrained profile, run off the end of the sampled array entirely. The
            // ceiling therefore comes from the same profile that sized the array.
            if (profile_.bindsSceneTextures && textureIdx >= profile_.sceneTextureCapacity)
            {
                Throw(std::runtime_error(fmt::format(
                    "scene texture capacity exhausted ({} registered, limit {}) while registering '{}'. "
                    "Raise Bindless::RES_SCENE_TEXTURE_CAPACITY in assets/shaders/common/BindlessTexture.slang, "
                    "or FBindlessProfile::Compatibility() when running the compatibility profile.",
                    textureIdx, profile_.sceneTextureCapacity, textureName)));
            }
            textureNameMap_[textureName] = {textureIdx, ETextureStatus::ETS_Loaded, lifetime};
            textureImages_.push_back(std::move(textureImage));
        }

        BindTexture(textureIdx, *textureImages_[textureIdx]);
        return textureIdx;
    }

    void GlobalTexturePool::ReleaseTexture(uint32_t textureIdx)
    {
        if (textureIdx >= textureImages_.size() || !textureImages_[textureIdx])
        {
            return;
        }

        // Descriptor sets may still be referenced by in-flight UI command buffers.
        device_.WaitIdle();

        textureImages_[textureIdx].reset();
        BindDefaultSampleTexture(textureIdx);
        if (textureIdx < textureCpuSources_.size())
        {
            textureCpuSources_[textureIdx] = {};
        }

        std::erase_if(textureNameMap_, [textureIdx](const auto& item)
        {
            return item.second.GlobalIdx_ == textureIdx;
        });
    }

    uint32_t GlobalTexturePool::TryGetTextureIndex(const std::string& textureName) const
    {
        if (textureNameMap_.find(textureName) != textureNameMap_.end())
        {
            return textureNameMap_.at(textureName).GlobalIdx_;
        }
        return -1;
    }

    uint32_t GlobalTexturePool::RequestNewTextureMemAsync(const std::string& texname, const std::string& mime, bool hdr,
                                                          const unsigned char* data, size_t bytelength, bool srgb,
                                                          ETextureLifetime lifetime)
    {
        uint32_t newTextureIdx = 0;
        if (textureNameMap_.find(texname) != textureNameMap_.end())
        {
            // Rebind textures that have transitioned to TextureUnLoaded.
            if(textureNameMap_[texname].Status_ == ETextureStatus::ETS_Unloaded)
            {
                textureNameMap_[texname].Status_ = ETextureStatus::ETS_Loaded;
                textureNameMap_[texname].Lifetime_ = lifetime;
                newTextureIdx = textureNameMap_[texname].GlobalIdx_;
            }
            else
            {
                textureNameMap_[texname].Lifetime_ = lifetime;
                // Return immediately if the texture is already loaded.
                return textureNameMap_[texname].GlobalIdx_;
            }
        }
        else
        {
            // Same ceiling as RegisterTexture: this is the path every scene texture actually takes,
            // so without the check here the ceiling was never enforced for scene content at all.
            // A profile that does not sample scene textures has no ceiling to enforce -- its
            // indices stay meaningful for materials and the bind is skipped instead.
            const auto candidateIdx = static_cast<uint32_t>(textureImages_.size());
            if (profile_.bindsSceneTextures && candidateIdx >= profile_.sceneTextureCapacity)
            {
                Throw(std::runtime_error(fmt::format(
                    "scene texture capacity exhausted ({} registered, limit {}) while requesting '{}'. "
                    "Raise Bindless::RES_SCENE_TEXTURE_CAPACITY in assets/shaders/common/BindlessTexture.slang.",
                    candidateIdx, profile_.sceneTextureCapacity, texname)));
            }

            textureImages_.emplace_back(nullptr);
            newTextureIdx = candidateIdx;
            textureNameMap_[texname] = { newTextureIdx, ETextureStatus::ETS_Loaded, lifetime };
        }

        // load parse bind texture into newTextureIdx with transfer queue

        uint8_t* copiedData = nullptr;
        if (bytelength > 0)
        {
            copiedData = new uint8_t[bytelength];
            memcpy(copiedData, data, bytelength);
        }
        if (textureCpuSources_.size() <= newTextureIdx)
        {
            textureCpuSources_.resize(static_cast<size_t>(newTextureIdx) + 1);
        }
        auto& cpuSource = textureCpuSources_[newTextureIdx];
        cpuSource.TextureName = texname;
        cpuSource.Mime = mime;
        cpuSource.Srgb = srgb;
        cpuSource.Hdr = hdr;
        cpuSource.Bytes.clear();
        if (copiedData && bytelength > 0)
        {
            cpuSource.Bytes.assign(copiedData, copiedData + bytelength);
        }
        const bool streamHDRAtLoad = hdr && hdrStreamingPolicy_ && hdrStreamingPolicy_();
        const EHDRTextureResidency initialHDRResidency =
            streamHDRAtLoad ? EHDRTextureResidency::LowestMip : EHDRTextureResidency::FullMip;
        if (hdr)
        {
            if (hdrTextureResidency_.size() <= newTextureIdx)
            {
                hdrTextureResidency_.resize(static_cast<size_t>(newTextureIdx) + 1);
            }
            auto& residency = hdrTextureResidency_[newTextureIdx];
            residency.TextureName = texname;
            residency.Lifetime = lifetime;
            residency.IsHDR = true;
            residency.Current = initialHDRResidency;
            residency.Target = initialHDRResidency;
            residency.Pending = true;
            residency.DemandFrames = 0;
            residency.LastTouchedFrame = 0;
        }
        auto textureLoadTask =
            [this, hdr, srgb, texname, mime, copiedData, bytelength, newTextureIdx, initialHDRResidency](Tasks::ResTask& task)
            {
                TextureTaskContext taskContext{};
                const auto timer = std::chrono::high_resolution_clock::now();

                // Load the texture in normal host memory.
                int width = 32;
                int height = 32;
                int channels = 4;
                uint8_t* stbdata = nullptr;
                uint8_t* pixels = nullptr;
                uint32_t size = 0;
                uint32_t miplevel = 1;
                VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
                bool textureCreated = false;
                ktxTexture2* kTexture = nullptr;
                ktx_error_code_e result;
                bool useWebPFree = false;

                auto createHdrPlaceholderTexture = [&]()
                {
                    // Dim neutral grey rather than black: a missing environment map still
                    // leaves the scene lit well enough to see what is going on.
                    static constexpr std::array<float, 4> kPlaceholderHdrPixel = {0.18f, 0.18f, 0.18f, 1.0f};

                    width = 1;
                    height = 1;
                    channels = 4;
                    size = static_cast<uint32_t>(kPlaceholderHdrPixel.size() * sizeof(float));
                    miplevel = 1;
                    format = VK_FORMAT_R32G32B32A32_SFLOAT;

                    hdrSphericalHarmonics_[newTextureIdx] = {};
                    textureImages_[newTextureIdx] = std::make_unique<TextureImage>(
                        commandPool_, width, height, miplevel, format,
                        reinterpret_cast<const unsigned char*>(kPlaceholderHdrPixel.data()), size);
                    textureCreated = true;
                };

                // load from ktx inside glb
                if (mime.find("image/ktx") != std::string::npos)
                {
                    auto loadKtxFromMemory = [&]() -> bool {
                        result = ktxTexture2_CreateFromMemory(copiedData, bytelength, KTX_TEXTURE_CREATE_CHECK_GLTF_BASISU_BIT, &kTexture);
                        if (KTX_SUCCESS != result) return false;
                        result = ktxTexture2_TranscodeBasis(
                            kTexture, supportsBCTextures_ ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32, 0);
                        if (KTX_SUCCESS != result) return false;
                        pixels = ktxTexture_GetData(ktxTexture(kTexture));

                        ktx_size_t offset;
                        ktxTexture_GetImageOffset(ktxTexture(kTexture), 0, 0, 0, &offset);
                        pixels += offset;
                        size = static_cast<uint32_t>(ktxTexture_GetImageSize(ktxTexture(kTexture), 0));

                        format = static_cast<VkFormat>(kTexture->vkFormat);
                        width = kTexture->baseWidth;
                        height = kTexture->baseHeight;
                        miplevel = 1;
                        return true;
                    };
                    
                    if (!loadKtxFromMemory())
                    {
                        SPDLOG_ERROR("load texture {} failed.", texname);
                    }
                }
                else if (mime.find("image/webp") != std::string::npos)
                {
                     stbdata = WebPDecodeRGBA(copiedData, bytelength, &width, &height);
                     if (stbdata)
                     {
                         size = width * height * 4;
                         pixels = stbdata;
                         format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                         miplevel = 1;
                         useWebPFree = true;
                     }
                     else
                     {
                         SPDLOG_ERROR("Failed to decode WebP image: {}", texname);
                     }
                }
                else
                {
                    std::hash<std::string> hasher;
                    // load from texture files
                    if (hdr)
                    {
                        const FHDRTexturePayload payload = LoadHDRTexturePayload(texname, copiedData, bytelength);
                        width = payload.Width;
                        height = payload.Height;
                        channels = 4;
                        miplevel = initialHDRResidency == EHDRTextureResidency::FullMip
                            ? std::max<uint32_t>(1, payload.MipLevels)
                            : 1;
                        format = payload.Format;
                        hdrSphericalHarmonics_[newTextureIdx] = payload.SH;
                        textureImages_[newTextureIdx] = CreateHDRTextureImage(commandPool_, payload, initialHDRResidency);
                        textureCreated = textureImages_[newTextureIdx] != nullptr;
                    }

                    if (hdr && !textureCreated)
                    {
                        // LoadHDRTexturePayload already tried cache + stbi decode; both failed.
                        createHdrPlaceholderTexture();
                    }
                    else if (!hdr)
                    {
                        // ldr texture, try cache fist
                        // hash the texname
                        std::string cacheFileName = Utilities::CookHelper::GetCookedFileName(fmt::format("{:016x}", hasher(texname)), "texktx");
#if ANDROID
                        // libktx's writer trips Android FORTIFY in appendLibId. The cache is optional,
                        // so keep the in-memory compression path and avoid reading/writing KTX files.
                        constexpr bool useKtxDiskCache = false;
#else
                        constexpr bool useKtxDiskCache = true;
#endif
                        if (!useKtxDiskCache || !std::filesystem::exists(cacheFileName))
                        {
                            // load from stbi and compress to ktx and cache
                            stbdata = stbi_load_from_memory(copiedData, static_cast<uint32_t>(bytelength), &width, &height, &channels, STBI_rgb_alpha);
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
                            if (useKtxDiskCache)
                            {
                                result = ktxTexture_WriteToNamedFile(ktxTexture(kTexture), cacheFileName.c_str());
                                if (result != KTX_SUCCESS)
                                {
                                    SPDLOG_WARN("Failed to cache KTX2 texture '{}': {}", texname, static_cast<int>(result));
                                }
                            }
                        }
                        else
                        {
                            result = ktxTexture2_CreateFromNamedFile(cacheFileName.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);
                            if (result != KTX_SUCCESS) Throw(std::runtime_error("failed to load ktx2 image "));
                        }

                        // next
                        result = ktxTexture2_TranscodeBasis(
                            kTexture, supportsBCTextures_ ? KTX_TTF_BC7_RGBA : KTX_TTF_RGBA32, 0);
                        if (result != KTX_SUCCESS) Throw(std::runtime_error("failed to transcode ktx2 image "));

                        pixels = ktxTexture_GetData(ktxTexture(kTexture));
                        ktx_size_t offset;
                        ktxTexture_GetImageOffset(ktxTexture(kTexture), 0, 0, 0, &offset);
                        pixels += offset;
                        size = static_cast<uint32_t>(ktxTexture_GetImageSize(ktxTexture(kTexture), 0));

                        format = supportsBCTextures_
                            ? (srgb ? VK_FORMAT_BC7_SRGB_BLOCK : VK_FORMAT_BC7_UNORM_BLOCK)
                            : (srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM);
                        width = kTexture->baseWidth;
                        height = kTexture->baseHeight;
                        miplevel = 1;
                    }
                }

                // create texture image
                if (!hdr)
                {
                    if (pixels == nullptr || size == 0)
                    {
                        static constexpr std::array<uint8_t, 4> kPlaceholderPixel = {255, 255, 255, 255};
                        width = 1;
                        height = 1;
                        miplevel = 1;
                        format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
                        pixels = const_cast<uint8_t*>(kPlaceholderPixel.data());
                        size = static_cast<uint32_t>(kPlaceholderPixel.size());
                    }
                    textureImages_[newTextureIdx] = std::make_unique<TextureImage>(commandPool_, width, height, miplevel, format, pixels, size);
                    textureCreated = true;
                }

                if (!textureCreated)
                {
                    throw std::runtime_error(fmt::format("failed to create texture '{}'", texname));
                }

                textureImages_[newTextureIdx]->SetDebugName(fmt::format("Texture {}", texname));
                BindTexture(newTextureIdx, *(textureImages_[newTextureIdx]));

                // clean up
                if (stbdata)
                {
                    if (useWebPFree) WebPFree(stbdata);
                    else stbi_image_free(stbdata);
                }
                
                if (kTexture) ktxTexture_Destroy(ktxTexture(kTexture));
                
                // transfer
                taskContext.textureId = newTextureIdx;
                taskContext.needFlushHDRSH = hdr;
                taskContext.hdrResidency = static_cast<uint8_t>(initialHDRResidency);
                taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - timer).count();
                std::string info = hdr
                    ? fmt::format("loaded {} ({} x {} x {}, {}) in {:.2f}ms", texname, width, height, miplevel,
                                  HDRResidencyName(initialHDRResidency), taskContext.elapsed * 1000.f)
                    : fmt::format("loaded {} ({} x {} x {}) in {:.2f}ms", texname, width, height, miplevel,
                                  taskContext.elapsed * 1000.f);
                std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
                task.SetContext(taskContext);
            };

        auto textureCompleteTask = [this, copiedData](Tasks::ResTask& task)
            {
                TextureTaskContext taskContext{};
                task.GetContext(taskContext);
                textureImages_[taskContext.textureId]->MainThreadPostLoading(mainThreadCommandPool_);
                SPDLOG_INFO("{}", taskContext.outputInfo.data());
                delete[] copiedData;

                if (taskContext.needFlushHDRSH)
                {
                    if (static_cast<size_t>(taskContext.textureId) < hdrTextureResidency_.size())
                    {
                        auto& residency = hdrTextureResidency_[taskContext.textureId];
                        residency.Current = static_cast<EHDRTextureResidency>(taskContext.hdrResidency);
                        residency.Target = residency.Current;
                        residency.Pending = false;
                    }
                    if (GlobalTexturePool::GetInstance()->hdrShUpdatedCallback_)
                    {
                        GlobalTexturePool::GetInstance()->hdrShUpdatedCallback_();
                    }
                }
            };

        if (textureWorkerUploadEnabled_)
        {
            Tasks::TaskCoordinator::GetInstance()->AddTask(
                std::move(textureLoadTask), std::move(textureCompleteTask), 0, "Texture load");
        }
        else
        {
            Tasks::TaskCoordinator::GetInstance()->AddMainThreadTask(
                std::move(textureLoadTask), std::move(textureCompleteTask), 0, "Texture load");
        }

        return newTextureIdx;
    }

    void GlobalTexturePool::TickHDRTextureResidency(
        uint32_t activeTextureIdx, bool hasSky, uint32_t frameIndex, bool streamingEnabled)
    {
        if (!streamingEnabled)
        {
            for (uint32_t textureIdx = 0; textureIdx < hdrTextureResidency_.size(); ++textureIdx)
            {
                const auto& residency = hdrTextureResidency_[textureIdx];
                if (residency.IsHDR && !residency.Pending && residency.Current != EHDRTextureResidency::FullMip)
                {
                    QueueHDRTextureResidency(textureIdx, EHDRTextureResidency::FullMip);
                }
            }
            return;
        }

        for (uint32_t textureIdx = 0; textureIdx < hdrTextureResidency_.size(); ++textureIdx)
        {
            auto& residency = hdrTextureResidency_[textureIdx];
            if (!residency.IsHDR)
            {
                continue;
            }

            const bool touched = hasSky && textureIdx == activeTextureIdx;
            if (touched)
            {
                residency.DemandFrames =
                    (residency.LastTouchedFrame + 1 == frameIndex) ? residency.DemandFrames + 1 : 1;
                residency.LastTouchedFrame = frameIndex;

                if (!residency.Pending
                    && residency.Current == EHDRTextureResidency::LowestMip
                    && residency.DemandFrames >= kHdrTexturePromotionFrames)
                {
                    QueueHDRTextureResidency(textureIdx, EHDRTextureResidency::FullMip);
                }
                continue;
            }

            if (!residency.Pending
                && residency.Current == EHDRTextureResidency::FullMip
                && frameIndex > residency.LastTouchedFrame + kHdrTextureDemotionFrames)
            {
                QueueHDRTextureResidency(textureIdx, EHDRTextureResidency::LowestMip);
            }
        }
    }

    void GlobalTexturePool::QueueHDRTextureResidency(uint32_t textureIdx, EHDRTextureResidency targetResidency)
    {
        if (textureIdx >= hdrTextureResidency_.size()
            || textureIdx >= textureImages_.size()
            || !hdrTextureResidency_[textureIdx].IsHDR)
        {
            return;
        }

        auto& residency = hdrTextureResidency_[textureIdx];
        if (residency.Pending || residency.Current == targetResidency)
        {
            return;
        }

        residency.Pending = true;
        residency.Target = targetResidency;
        const std::string textureName = residency.TextureName;

        auto sourceData = std::make_shared<std::vector<uint8_t>>();
        if (!std::filesystem::exists(HDRCacheFileName(textureName)))
        {
            Utilities::Package::FPackageFileSystem::GetInstance().LoadFile(textureName, *sourceData);
        }

        auto textureResidencyTask =
            [this, textureIdx, textureName, targetResidency, sourceData](Tasks::ResTask& task)
            {
                TextureTaskContext taskContext {};
                const auto timer = std::chrono::high_resolution_clock::now();

                const uint8_t* data = sourceData->empty() ? nullptr : sourceData->data();
                const FHDRTexturePayload payload = LoadHDRTexturePayload(textureName, data, sourceData->size());
                auto textureImage = CreateHDRTextureImage(commandPool_, payload, targetResidency);
                if (!textureImage)
                {
                    // The source went missing between load and streaming; keep whatever
                    // image is currently bound instead of unbinding the slot.
                    SPDLOG_WARN("HDR texture '{}' could not be re-created for residency change; keeping current image.",
                                textureName);
                    if (textureIdx < hdrTextureResidency_.size())
                    {
                        hdrTextureResidency_[textureIdx].Pending = false;
                    }
                    return;
                }
                textureImage->SetDebugName(fmt::format("Texture {}", textureName));

                device_.WaitIdle();
                textureImages_[textureIdx] = std::move(textureImage);
                hdrSphericalHarmonics_[textureIdx] = payload.SH;

                taskContext.textureId = static_cast<int32_t>(textureIdx);
                taskContext.needFlushHDRSH = true;
                taskContext.hdrResidency = static_cast<uint8_t>(targetResidency);
                taskContext.elapsed = std::chrono::duration<float, std::chrono::seconds::period>(
                    std::chrono::high_resolution_clock::now() - timer).count();
                const std::string info = fmt::format(
                    "[HDRTextureResidency] {} -> {} in {:.2f}ms",
                    textureName, HDRResidencyName(targetResidency), taskContext.elapsed * 1000.f);
                std::copy(info.begin(), info.end(), taskContext.outputInfo.data());
                task.SetContext(taskContext);
            };

        auto textureResidencyCompleteTask = [this](Tasks::ResTask& task)
            {
                TextureTaskContext taskContext {};
                task.GetContext(taskContext);
                const uint32_t textureIdx = static_cast<uint32_t>(taskContext.textureId);
                if (textureIdx < textureImages_.size() && textureImages_[textureIdx])
                {
                    textureImages_[textureIdx]->MainThreadPostLoading(mainThreadCommandPool_);
                    BindTexture(textureIdx, *textureImages_[textureIdx]);
                }

                if (textureIdx < hdrTextureResidency_.size())
                {
                    auto& residency = hdrTextureResidency_[textureIdx];
                    residency.Current = static_cast<EHDRTextureResidency>(taskContext.hdrResidency);
                    residency.Target = residency.Current;
                    residency.Pending = false;
                    if (residency.Current == EHDRTextureResidency::LowestMip)
                    {
                        residency.DemandFrames = 0;
                    }
                }

                SPDLOG_INFO("{}", taskContext.outputInfo.data());
                if (GlobalTexturePool::GetInstance()->hdrShUpdatedCallback_)
                {
                    GlobalTexturePool::GetInstance()->hdrShUpdatedCallback_();
                }
            };

        if (textureWorkerUploadEnabled_)
        {
            Tasks::TaskCoordinator::GetInstance()->AddTask(
                std::move(textureResidencyTask), std::move(textureResidencyCompleteTask), 0,
                "HDR texture residency");
        }
        else
        {
            Tasks::TaskCoordinator::GetInstance()->AddMainThreadTask(
                std::move(textureResidencyTask), std::move(textureResidencyCompleteTask), 0,
                "HDR texture residency");
        }
    }

    void GlobalTexturePool::FreeTransientTextures()
    {
        // make sure the binded image not in use
        device_.WaitIdle();
        
        for (auto& textureGroup : textureNameMap_)
        {
            if (textureGroup.second.Lifetime_ == ETextureLifetime::ETL_Persistent)
            {
                continue;
            }

            const uint32_t textureIdx = textureGroup.second.GlobalIdx_;
            if (textureIdx >= textureImages_.size())
            {
                continue;
            }

            if (textureImages_[textureIdx])
            {
                textureImages_[textureIdx].reset();
                BindTexture(textureIdx, *defaultWhiteTexture_);
            }

            textureGroup.second.Status_ = ETextureStatus::ETS_Unloaded;
        }
    }

    void GlobalTexturePool::CreateDefaultTextures()
    {
        defaultWhiteTexture_ = std::make_unique<TextureImage>(commandPool_, 16, 16, 1, VK_FORMAT_R8G8B8A8_UNORM, nullptr, 0);
        defaultWhiteTexture_->SetDebugName("Texture DefaultWhite");
    }

    GlobalTexturePool* GlobalTexturePool::instance_ = nullptr;
}
