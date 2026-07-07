#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Preview/RenderViewServices.hpp"

namespace Vulkan
{
    RenderViewServices::RenderViewServices(VulkanBaseRenderer& renderer)
        : assetThumbnails_(std::make_unique<AssetThumbnailRenderer>(renderer))
        , offscreenViews_(std::make_unique<OffscreenRenderViewController>(renderer))
    {
    }

    RenderViewServices::~RenderViewServices() = default;

    void RenderViewServices::BeforeNextFrame()
    {
        assetThumbnails_->BeforeNextFrame();
    }

    void RenderViewServices::OnMainSceneChanged()
    {
        offscreenViews_->OnMainSceneChanged();
        assetThumbnails_->OnMainSceneChanged();
    }

    void RenderViewServices::OnHdrShUpdated()
    {
        assetThumbnails_->OnHdrShUpdated();
    }

    void RenderViewServices::OnSwapChainResourcesInvalidated(const bool releaseOffscreenSampledOutputs)
    {
        offscreenViews_->OnSwapChainResourcesInvalidated(releaseOffscreenSampledOutputs);
        assetThumbnails_->OnSwapChainResourcesInvalidated();
    }

    bool RenderViewServices::HasWork() const
    {
        return assetThumbnails_->HasPendingThumbnail() ||
               assetThumbnails_->HasMaterialPreviewWork() ||
               offscreenViews_->HasWork();
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
        assetThumbnails_->ScheduleMaterialPreview(commandBuffer, imageIndex);
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
