#pragma once

#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Rendering/Preview/RenderViewServices.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Assets
{
    class Model;
    class Scene;
}

namespace Vulkan
{
    class FrameBuffer;
    class RenderImage;
    class RenderView;
    class Sampler;
    class VulkanBaseRenderer;

    class AssetThumbnailRenderer final : public IRenderViewProvider
    {
    public:
        // Slot ranges come from the bindless registry (assets/shaders/common/BindlessTexture.slang);
        // do not reintroduce literals here -- that is how the old 64500 collision with the remote
        // composite range happened.
        static constexpr uint32_t kThumbnailSampleSlotBase =
            static_cast<uint32_t>(Assets::Bindless::RES_THUMBNAIL_BASE);
        static constexpr uint32_t kThumbnailMaxSlots =
            static_cast<uint32_t>(Assets::Bindless::RES_THUMBNAIL_COUNT);
        static constexpr uint32_t kMaterialPreviewSampleSlot =
            static_cast<uint32_t>(Assets::Bindless::RES_MATERIAL_PREVIEW);

        explicit AssetThumbnailRenderer(VulkanBaseRenderer& renderer);
        ~AssetThumbnailRenderer();

        uint32_t RequestMaterialThumbnail(uint32_t materialIndex, uint64_t materialHash);
        uint32_t RequestMaterialThumbnail(uint32_t materialIndex, const Assets::FMaterial& material);
        uint32_t RequestMeshThumbnail(uint32_t modelIndex, uint64_t modelHash);
        uint32_t RequestMeshThumbnail(uint32_t modelIndex, const Assets::Model& model);
        // Request a thumbnail for a generated SCAD source that contains one
        // module call. The source path is kept by the cache and evaluated only
        // when the thumbnail is scheduled, so the UI remains cheap while
        // scrolling a large kit library.
        uint32_t RequestScadKitThumbnail(uint32_t thumbnailIndex, const std::string& sourcePath, uint64_t sourceHash);

        void BeforeNextFrame() override;
        void OnMainSceneChanged() override;
        void OnHdrShUpdated() override;
        void OnSwapChainResourcesInvalidated(bool releaseSampledOutputs) override;
        bool HasWork() const override { return HasPendingThumbnail() || HasMaterialPreviewWork(); }
        bool ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
        bool HasPendingThumbnail() const;
        bool ScheduleNextThumbnail(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        void SetEnabled(bool enabled);
        bool IsEnabled() const { return materialPreviewEnabled_; }
        void SetRenderExtent(VkExtent2D extent);
        VkExtent2D RenderExtent() const { return materialPreviewExtent_; }
        void SetPreviewMaterial(const Assets::FMaterial& material);
        void SetCameraOrbit(float yawRadians, float pitchRadians, float distance);
        uint32_t SampleSlot() const { return kMaterialPreviewSampleSlot; }
        bool IsReady() const { return materialPreviewTarget_.offscreenImage != nullptr; }
        bool HasMaterialPreviewWork() const { return materialPreviewEnabled_ && materialPreviewDirty_; }
        bool ScheduleMaterialPreview(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    private:
        static constexpr VkExtent2D kThumbnailExtent{128, 128};

        enum class EThumbnailKind : uint32_t
        {
            Material,
            Mesh,
            ScadKit,
            Count,
        };
        struct FThumbnailKey
        {
            EThumbnailKind kind = EThumbnailKind::Material;
            uint32_t assetIndex = 0;

            constexpr bool operator==(const FThumbnailKey&) const = default;
        };

        struct FThumbnailKeyHash
        {
            size_t operator()(const FThumbnailKey& key) const
            {
                return (static_cast<size_t>(key.kind) << 32u) ^ static_cast<size_t>(key.assetIndex);
            }
        };

        struct FThumbnailSlot
        {
            FThumbnailKey key{};
            uint64_t contentHash = 0;
            uint64_t lastRequestedFrame = 0;
            uint64_t generation = 0;
            std::unique_ptr<RenderImage> image;
            std::string sourcePath;
            bool occupied = false;
            bool ready = false;
            bool pending = false;
        };

        struct FRetiredScene
        {
            std::unique_ptr<Assets::Scene> scene;
            uint64_t releaseAfterSubmitSerial = 0;
        };

        static uint64_t HashMaterialThumbnail(const Assets::FMaterial& material);
        static uint64_t HashMeshThumbnail(const Assets::Model& model);
        uint32_t RequestThumbnail(
            EThumbnailKind kind,
            uint32_t assetIndex,
            uint64_t assetHash,
            const std::string& sourcePath = {});
        uint32_t AcquireThumbnailSlot(const FThumbnailKey& key, uint64_t currentFrame);
        void QueueThumbnail(uint32_t poolIndex);
        void ClearThumbnailCache();
        void EnqueueExistingThumbnailImages();
        void RetireScene(std::unique_ptr<Assets::Scene>& scene);
        void CollectRetiredScenes();
        std::unique_ptr<Assets::Scene> CreateMaterialSphereScene(
            Assets::FMaterial material,
            const char* nodeName,
            const char* cameraName,
            bool sunEnabled,
            const Assets::Camera& camera);
        Assets::Camera BuildMaterialPreviewCamera() const;
        void EnsureMaterialThumbnailScene();
        void EnsureMaterialPreviewScene();
        void RebuildMeshThumbnailScene(const Assets::Model& model);
        bool RebuildScadKitThumbnailScene(const std::string& sourcePath);
        void EnsureThumbnailRenderTarget();
        void EnsureMaterialPreviewRenderTarget();
        RenderImage& EnsureThumbnailImage(uint32_t poolIndex, const char* debugName);
        void CopyThumbnailViewOutput(VkCommandBuffer commandBuffer, RenderView& view, RenderImage& dst);
        void CopyMaterialPreviewOutput(VkCommandBuffer commandBuffer, RenderView& view);
        RenderView* ThumbnailView() const;
        RenderView* MaterialPreviewView() const;

        VulkanBaseRenderer& renderer_;
        FRenderViewHandle thumbnailRenderView_;
        FRenderViewHandle materialPreviewView_;
        std::unique_ptr<Assets::Scene> thumbnailScene_;
        std::unique_ptr<Assets::Scene> materialPreviewScene_;
        std::vector<FThumbnailSlot> thumbnailSlots_;
        std::unordered_map<FThumbnailKey, uint32_t, FThumbnailKeyHash> thumbnailLookup_;
        std::vector<uint32_t> pendingThumbnails_;
        std::vector<FRetiredScene> retiredScenes_;
        EThumbnailKind thumbnailSceneKind_ = EThumbnailKind::Material;
        bool thumbnailSceneReady_ = false;
        FRenderViewTargetResources thumbnailTarget_;
        FRenderViewTargetResources materialPreviewTarget_;
        VkExtent2D materialPreviewExtent_{256, 256};
        bool materialPreviewEnabled_ = false;
        bool materialPreviewSceneReady_ = false;
        bool materialPreviewDirty_ = true;
        float materialPreviewYaw_ = 0.0f;
        float materialPreviewPitch_ = 0.0f;
        float materialPreviewDistance_ = 4.0f;
        Assets::FMaterial materialPreview_{};
    };
}

namespace EditorPreview
{
    // Get-or-create the editor's thumbnail/material-preview provider on this renderer.
    Vulkan::AssetThumbnailRenderer& AssetThumbnails(Vulkan::VulkanBaseRenderer& renderer);
}
