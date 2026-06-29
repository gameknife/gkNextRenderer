#pragma once

#include "Engine/Rendering/Preview/AssetThumbnailRenderer.hpp"
#include "Engine/Rendering/Preview/OffscreenRenderViewController.hpp"

#include <memory>

namespace Vulkan
{
    class VulkanBaseRenderer;

    class RenderPreviewServices final
    {
    public:
        explicit RenderPreviewServices(VulkanBaseRenderer& renderer);
        ~RenderPreviewServices();

        AssetThumbnailRenderer& AssetThumbnails() { return *assetThumbnails_; }
        const AssetThumbnailRenderer& AssetThumbnails() const { return *assetThumbnails_; }
        OffscreenRenderViewController& OffscreenViews() { return *offscreenViews_; }
        const OffscreenRenderViewController& OffscreenViews() const { return *offscreenViews_; }

        void BeforeNextFrame();
        void OnMainSceneChanged();
        void OnSwapChainResourcesInvalidated(bool releaseOffscreenSampledOutputs);
        bool HasPendingThumbnail() const;
        bool HasOffscreenWork(bool includeDebugOverlay) const;
        bool ScheduleNextThumbnail(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void ScheduleOffscreenViews(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void ClearOffscreenFrameRequests();

    private:
        std::unique_ptr<AssetThumbnailRenderer> assetThumbnails_;
        std::unique_ptr<OffscreenRenderViewController> offscreenViews_;
    };
}
