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

    bool RenderPreviewServices::HasPendingThumbnail() const
    {
        return assetThumbnails_->HasPendingThumbnail();
    }

    bool RenderPreviewServices::HasOffscreenWork(const bool includeDebugOverlay) const
    {
        return offscreenViews_->HasWork(includeDebugOverlay);
    }

    bool RenderPreviewServices::ScheduleNextThumbnail(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        return assetThumbnails_->ScheduleNextThumbnail(commandBuffer, imageIndex);
    }

    void RenderPreviewServices::ScheduleOffscreenViews(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        offscreenViews_->ScheduleViews(commandBuffer, imageIndex);
    }

    void RenderPreviewServices::ClearOffscreenFrameRequests()
    {
        offscreenViews_->ClearFrameRequests();
    }
}
