#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Preview/RenderPreviewServices.hpp"

namespace Vulkan
{
    RenderPreviewServices::RenderPreviewServices(VulkanBaseRenderer& renderer)
        : assetThumbnails_(std::make_unique<AssetThumbnailRenderer>(renderer))
        , offscreenViews_(std::make_unique<OffscreenRenderViewController>(renderer))
    {
    }

    RenderPreviewServices::~RenderPreviewServices() = default;

    void RenderPreviewServices::BeforeNextFrame()
    {
        assetThumbnails_->BeforeNextFrame();
    }

    void RenderPreviewServices::OnMainSceneChanged()
    {
        offscreenViews_->OnMainSceneChanged();
        assetThumbnails_->OnMainSceneChanged();
    }

    void RenderPreviewServices::OnSwapChainResourcesInvalidated(const bool releaseOffscreenSampledOutputs)
    {
        offscreenViews_->OnSwapChainResourcesInvalidated(releaseOffscreenSampledOutputs);
        assetThumbnails_->OnSwapChainResourcesInvalidated();
    }

    bool RenderPreviewServices::HasWork(const bool includeDebugOverlay) const
    {
        return assetThumbnails_->HasPendingThumbnail() || offscreenViews_->HasWork(includeDebugOverlay);
    }

    void RenderPreviewServices::ScheduleViews(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        const bool includeDebugOverlay)
    {
        uint32_t scheduledTransientPreviews = 0;
        if (schedulePolicy_.maxTransientPreviewsPerFrame > 0 &&
            assetThumbnails_->ScheduleNextThumbnail(commandBuffer, imageIndex))
        {
            ++scheduledTransientPreviews;
        }
        if (scheduledTransientPreviews > 0 &&
            schedulePolicy_.deferOffscreenViewsWhenTransientPreviewScheduled)
        {
            return;
        }
        if (!includeDebugOverlay && !offscreenViews_->HasWork(false))
        {
            return;
        }
        offscreenViews_->ScheduleViews(commandBuffer, imageIndex);
    }

    void RenderPreviewServices::ClearOffscreenFrameRequests()
    {
        offscreenViews_->ClearFrameRequests();
    }
}
