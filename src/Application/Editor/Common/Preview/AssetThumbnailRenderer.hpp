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
        static constexpr uint32_t kMaterialThumbnailSampleSlotBase = 64000;
        static constexpr uint32_t kMaterialThumbnailMaxSlots = 512;
        static constexpr uint32_t kMeshThumbnailSampleSlotBase = 63200;
        static constexpr uint32_t kMeshThumbnailMaxSlots = 512;
        static constexpr uint32_t kMaterialPreviewSampleSlot = 64600;

        explicit AssetThumbnailRenderer(VulkanBaseRenderer& renderer);
        ~AssetThumbnailRenderer();

        uint32_t RequestMaterialThumbnail(uint32_t materialIndex, uint64_t materialHash);
        uint32_t RequestMeshThumbnail(uint32_t modelIndex, uint64_t modelHash);

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
        bool HasMaterialPreviewWork() const { return materialPreviewEnabled_; }
        bool ScheduleMaterialPreview(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    private:
        static constexpr VkExtent2D kThumbnailExtent{128, 128};

        enum class EThumbnailKind : uint32_t
        {
            Material,
            Mesh,
            Count,
        };
        static constexpr size_t kThumbnailKindCount = 2;

        struct FThumbnailCache
        {
            uint32_t sampleSlotBase = 0;
            uint32_t maxSlots = 0;
            std::vector<std::unique_ptr<RenderImage>> images;
            std::vector<uint64_t> hashes;
            std::vector<uint32_t> pending;
        };

        static constexpr size_t ThumbnailKindIndex(EThumbnailKind kind)
        {
            return static_cast<size_t>(kind);
        }

        FThumbnailCache& ThumbnailCache(EThumbnailKind kind);
        const FThumbnailCache& ThumbnailCache(EThumbnailKind kind) const;
        uint32_t RequestThumbnail(EThumbnailKind kind, uint32_t assetIndex, uint64_t assetHash);
        void ClearThumbnailCaches();
        void EnqueueExistingThumbnailImages(FThumbnailCache& cache);
        bool HasPendingThumbnail(EThumbnailKind kind) const;
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
        void EnsureThumbnailRenderTarget();
        void EnsureMaterialPreviewRenderTarget();
        RenderImage& EnsureThumbnailImage(FThumbnailCache& cache, uint32_t assetIndex, const char* debugName);
        void CopyThumbnailViewOutput(VkCommandBuffer commandBuffer, RenderView& view, RenderImage& dst);
        void CopyMaterialPreviewOutput(VkCommandBuffer commandBuffer, RenderView& view);
        bool ScheduleNextThumbnail(EThumbnailKind kind, VkCommandBuffer commandBuffer, uint32_t imageIndex);

        VulkanBaseRenderer& renderer_;
        RenderView* thumbnailRenderView_ = nullptr;
        RenderView* materialPreviewView_ = nullptr;
        std::unique_ptr<Assets::Scene> thumbnailScene_;
        std::unique_ptr<Assets::Scene> materialPreviewScene_;
        std::array<FThumbnailCache, kThumbnailKindCount> thumbnailCaches_{};
        EThumbnailKind thumbnailSceneKind_ = EThumbnailKind::Material;
        bool thumbnailSceneReady_ = false;
        bool releaseThumbnailView_ = false;
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
