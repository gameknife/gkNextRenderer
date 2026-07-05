#pragma once

#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/RenderViewResources.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <vulkan/vulkan.h>

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

    class AssetThumbnailRenderer final
    {
    public:
        static constexpr uint32_t kMaterialThumbnailSampleSlotBase = 64000;
        static constexpr uint32_t kMaterialThumbnailMaxSlots = 512;
        static constexpr uint32_t kMeshThumbnailSampleSlotBase = 63200;
        static constexpr uint32_t kMeshThumbnailMaxSlots = 512;

        explicit AssetThumbnailRenderer(VulkanBaseRenderer& renderer);
        ~AssetThumbnailRenderer();

        uint32_t RequestMaterialThumbnail(uint32_t materialIndex, uint64_t materialHash);
        uint32_t RequestMeshThumbnail(uint32_t modelIndex, uint64_t modelHash);

        void BeforeNextFrame();
        void OnMainSceneChanged();
        void OnHdrShUpdated();
        void OnSwapChainResourcesInvalidated();
        bool HasPendingThumbnail() const;
        bool ScheduleNextThumbnail(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    private:
        static constexpr VkExtent2D kThumbnailExtent{128, 128};

        void EnsureMaterialThumbnailScene();
        void RebuildMeshThumbnailScene(const Assets::Model& model);
        void EnsureThumbnailRenderTarget();
        void CopyThumbnailViewOutput(VkCommandBuffer commandBuffer, RenderView& view, RenderImage& dst);
        bool HasPendingMaterialThumbnail() const { return !pendingMaterialThumbnails_.empty(); }
        bool HasPendingMeshThumbnail() const { return !pendingMeshThumbnails_.empty(); }
        bool ScheduleNextMaterialThumbnail(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        bool ScheduleNextMeshThumbnail(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        VulkanBaseRenderer& renderer_;
        RenderView* thumbnailRenderView_ = nullptr;
        std::unique_ptr<Assets::Scene> materialThumbnailScene_;
        std::vector<std::unique_ptr<RenderImage>> materialThumbnailImages_;
        std::vector<uint64_t> materialThumbnailHashes_;
        std::vector<uint32_t> pendingMaterialThumbnails_;
        bool materialThumbnailSceneReady_ = false;
        std::unique_ptr<Assets::Scene> meshThumbnailScene_;
        std::vector<std::unique_ptr<RenderImage>> meshThumbnailImages_;
        std::vector<uint64_t> meshThumbnailHashes_;
        std::vector<uint32_t> pendingMeshThumbnails_;
        FRenderViewTargetResources thumbnailTarget_;
    };
}
