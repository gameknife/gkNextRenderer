#pragma once

#include "Engine/Rendering/Preview/AssetThumbnailRenderer.hpp"
#include "Engine/Rendering/Preview/OffscreenRenderViewController.hpp"

#include <memory>

namespace Vulkan
{
    class VulkanBaseRenderer;

    class RenderViewServices final
    {
    public:
        struct FSchedulePolicy
        {
            uint32_t maxTransientPreviewsPerFrame = 1;
            bool deferOffscreenViewsWhenTransientPreviewScheduled = true;
        };

        explicit RenderViewServices(VulkanBaseRenderer& renderer);
        ~RenderViewServices();

        AssetThumbnailRenderer& AssetThumbnails() { return *assetThumbnails_; }
        const AssetThumbnailRenderer& AssetThumbnails() const { return *assetThumbnails_; }
        AssetThumbnailRenderer& MaterialPreview() { return *assetThumbnails_; }
        const AssetThumbnailRenderer& MaterialPreview() const { return *assetThumbnails_; }
        OffscreenRenderViewController& OffscreenViews() { return *offscreenViews_; }
        const OffscreenRenderViewController& OffscreenViews() const { return *offscreenViews_; }

        void BeforeNextFrame();
        void OnMainSceneChanged();
        void OnHdrShUpdated();
        void OnSwapChainResourcesInvalidated(bool releaseOffscreenSampledOutputs);
        bool HasWork() const;
        void ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void ClearOffscreenFrameRequests();

    private:
        FSchedulePolicy schedulePolicy_{};
        std::unique_ptr<AssetThumbnailRenderer> assetThumbnails_;
        std::unique_ptr<OffscreenRenderViewController> offscreenViews_;
    };
}
