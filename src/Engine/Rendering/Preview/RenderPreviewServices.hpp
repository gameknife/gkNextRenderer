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
        struct FSchedulePolicy
        {
            uint32_t maxTransientPreviewsPerFrame = 1;
            bool deferOffscreenViewsWhenTransientPreviewScheduled = true;
        };

        explicit RenderPreviewServices(VulkanBaseRenderer& renderer);
        ~RenderPreviewServices();

        AssetThumbnailRenderer& AssetThumbnails() { return *assetThumbnails_; }
        const AssetThumbnailRenderer& AssetThumbnails() const { return *assetThumbnails_; }
        OffscreenRenderViewController& OffscreenViews() { return *offscreenViews_; }
        const OffscreenRenderViewController& OffscreenViews() const { return *offscreenViews_; }

        void BeforeNextFrame();
        void OnMainSceneChanged();
        void OnSwapChainResourcesInvalidated(bool releaseOffscreenSampledOutputs);
        bool HasWork(bool includeDebugOverlay) const;
        void ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool includeDebugOverlay);
        void ClearOffscreenFrameRequests();

    private:
        FSchedulePolicy schedulePolicy_{};
        std::unique_ptr<AssetThumbnailRenderer> assetThumbnails_;
        std::unique_ptr<OffscreenRenderViewController> offscreenViews_;
    };
}
