#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Preview/RenderViewServices.hpp"

namespace Vulkan
{
    RenderViewServices::RenderViewServices(VulkanBaseRenderer& renderer)
        : assetThumbnails_(std::make_unique<AssetThumbnailRenderer>(renderer))
        , materialPreview_(std::make_unique<MaterialPreviewRenderer>(renderer))
        , offscreenViews_(std::make_unique<OffscreenRenderViewController>(renderer))
    {
    }

    RenderViewServices::~RenderViewServices() = default;

    void RenderViewServices::BeforeNextFrame()
    {
        assetThumbnails_->BeforeNextFrame();
        materialPreview_->BeforeNextFrame();
    }

    void RenderViewServices::OnMainSceneChanged()
    {
        materialPreview_->OnMainSceneChanged();
        offscreenViews_->OnMainSceneChanged();
        assetThumbnails_->OnMainSceneChanged();
    }

    void RenderViewServices::OnHdrShUpdated()
    {
        materialPreview_->OnHdrShUpdated();
        assetThumbnails_->OnHdrShUpdated();
    }

    void RenderViewServices::OnSwapChainResourcesInvalidated(const bool releaseOffscreenSampledOutputs)
    {
        materialPreview_->OnSwapChainResourcesInvalidated();
        offscreenViews_->OnSwapChainResourcesInvalidated(releaseOffscreenSampledOutputs);
        assetThumbnails_->OnSwapChainResourcesInvalidated();
    }

    bool RenderViewServices::HasWork() const
    {
        return assetThumbnails_->HasPendingThumbnail() || materialPreview_->HasWork() || offscreenViews_->HasWork();
    }

    void RenderViewServices::ScheduleViews(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        uint32_t scheduledTransientPreviews = 0;
        if (schedulePolicy_.maxTransientPreviewsPerFrame > 0 &&
            assetThumbnails_->ScheduleNextThumbnail(commandBuffer, imageIndex))
        {
            ++scheduledTransientPreviews;
        }
        materialPreview_->ScheduleView(commandBuffer, imageIndex);
        if (scheduledTransientPreviews > 0 &&
            schedulePolicy_.deferOffscreenViewsWhenTransientPreviewScheduled)
        {
            return;
        }
        if (!offscreenViews_->HasWork())
        {
            return;
        }
        offscreenViews_->ScheduleViews(commandBuffer, imageIndex);
    }

    void RenderViewServices::ClearOffscreenFrameRequests()
    {
        offscreenViews_->ClearFrameRequests();
    }
}
