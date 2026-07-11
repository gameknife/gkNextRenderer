#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/RenderView.hpp"

#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Utilities/Exception.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"

namespace Vulkan
{
    const char* GetHistoryInvalidationReasonName(const EHistoryInvalidationReason reason)
    {
        switch (reason)
        {
        case EHistoryInvalidationReason::Initial: return "initial";
        case EHistoryInvalidationReason::SceneChanged: return "scene-changed";
        case EHistoryInvalidationReason::RendererChanged: return "renderer-changed";
        case EHistoryInvalidationReason::ExtentChanged: return "extent-changed";
        case EHistoryInvalidationReason::CameraCut: return "camera-cut";
        case EHistoryInvalidationReason::TemporalConfigChanged: return "temporal-config-changed";
        case EHistoryInvalidationReason::ViewReused: return "view-reused";
        case EHistoryInvalidationReason::SwapchainRecreated: return "swapchain-recreated";
        }
        return "unknown";
    }
    namespace
    {
        void EmitTrackedImageBarrier(
            VkCommandBuffer commandBuffer,
            PipelineCommon::FResourceStateTracker& tracker,
            const PipelineCommon::FImageUse& use,
            const VkImage image,
            const std::string_view passName)
        {
            const auto barrier = tracker.Use(use, passName);
            if (!barrier)
            {
                return;
            }

            VkImageMemoryBarrier vkBarrier{};
            vkBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            vkBarrier.srcAccessMask = barrier->srcAccess;
            vkBarrier.dstAccessMask = barrier->dstAccess;
            vkBarrier.oldLayout = barrier->oldLayout;
            vkBarrier.newLayout = barrier->newLayout;
            vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            vkBarrier.image = image;
            vkBarrier.subresourceRange = barrier->range;
            vkCmdPipelineBarrier(commandBuffer, barrier->srcStages, barrier->dstStages, 0,
                                 0, nullptr, 0, nullptr, 1, &vkBarrier);
        }
    }

    FRenderViewTargetResources::~FRenderViewTargetResources() = default;
    FRenderViewTargetResources::FRenderViewTargetResources(FRenderViewTargetResources&&) noexcept = default;
    FRenderViewTargetResources& FRenderViewTargetResources::operator=(FRenderViewTargetResources&&) noexcept = default;

    void FRenderViewTargetResources::ResetSwapChainResources(const bool releaseSampledOutput)
    {
        visibilityFramebuffer.reset();
        if (releaseSampledOutput)
        {
            offscreenImage.reset();
            offscreenSampler.reset();
            outputSampleSlot = std::numeric_limits<uint32_t>::max();
        }
    }

    RenderViewResourceFactory::RenderViewResourceFactory(VulkanBaseRenderer& renderer)
        : renderer_(renderer)
    {
    }

    RenderView& RenderViewResourceFactory::EnsureView(
        RenderView*& view,
        const FViewDesc& desc,
        std::string debugName,
        const bool copyObjectIdHistory)
    {
        if (view == nullptr)
        {
            view = renderer_.renderViews_->CreateView(desc, debugName);
            if (view == nullptr)
            {
                Throw(std::runtime_error("failed to allocate RenderView bank"));
            }
            view->CreateSwapChain(renderer_.SwapChain());
        }

        view->SetDebugName(std::move(debugName));
        view->SetRenderExtent(desc.renderExtent);
        view->SetSubrect(desc.subrect);
        view->SetCopyObjectIdHistory(copyObjectIdHistory);
        return *view;
    }

    std::unique_ptr<FrameBuffer> RenderViewResourceFactory::RebuildVisibilityFramebuffer(
        RenderView& view,
        const VkExtent2D extent)
    {
        renderer_.CreateRenderTargetBank(view.RtBankBase(), extent);
        auto frameBuffer = std::make_unique<FrameBuffer>(
            extent,
            renderer_.GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImageView(),
            renderer_.overlay_.visibilityPipeline->RenderPass());
        view.SetAllocatedVisibilityFramebuffer(frameBuffer.get(), extent);
        return frameBuffer;
    }

    std::unique_ptr<RenderImage> RenderViewResourceFactory::CreateSampledColorImage(
        const VkExtent2D extent,
        const char* debugName)
    {
        return std::make_unique<RenderImage>(
            renderer_.Device(),
            extent,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            false,
            debugName);
    }

    std::unique_ptr<Sampler> RenderViewResourceFactory::CreateClampSampler()
    {
        Vulkan::SamplerConfig samplerConfig{};
        samplerConfig.AddressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerConfig.AddressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerConfig.AnisotropyEnable = false;
        return std::make_unique<Vulkan::Sampler>(renderer_.Device(), samplerConfig);
    }

    void RenderViewResourceFactory::BindSampledColorImage(
        const uint32_t sampleSlot,
        RenderImage& image,
        Sampler& sampler)
    {
        renderer_.ctx_.globalTexturePool->BindSampleTexture(sampleSlot, image.GetImageView(), sampler);
    }

    void RenderViewResourceFactory::EnsureSampledOffscreenTarget(
        RenderView& view,
        FRenderViewTargetResources& target,
        const VkExtent2D extent,
        const uint32_t sampleSlot,
        const char* debugName)
    {
        if (view.AllocatedExtent().width == extent.width &&
            view.AllocatedExtent().height == extent.height &&
            view.VisibilityFramebuffer() != nullptr &&
            target.offscreenImage != nullptr)
        {
            return;
        }

        target.visibilityFramebuffer.reset();
        target.offscreenImage.reset();
        target.visibilityFramebuffer = RebuildVisibilityFramebuffer(view, extent);
        target.offscreenImage = CreateSampledColorImage(extent, debugName);
        if (!target.offscreenSampler)
        {
            target.offscreenSampler = CreateClampSampler();
        }
        target.outputSampleSlot = sampleSlot;
        BindSampledColorImage(target.outputSampleSlot, *target.offscreenImage, *target.offscreenSampler);
        view.InvalidateTemporalHistory(EHistoryInvalidationReason::ExtentChanged);
    }

    bool RenderViewResourceFactory::CopyDenoisedOutputToImage(
        VkCommandBuffer commandBuffer,
        RenderView& view,
        RenderImage& dst,
        const VkFilter filter)
    {
        const RenderImage* src = renderer_.GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_DENOISED);
        if (src == nullptr)
        {
            return false;
        }

        const VkExtent2D extent = view.RenderExtent();
        auto& viewStates = view.ResourceStates();
        auto& outputStates = renderer_.AuxiliaryImageStates();
        const PipelineCommon::FImageHandle srcHandle{
            static_cast<uint64_t>(view.RtBankBase() + Assets::Bindless::RT_DENOISED) + 1u};
        const PipelineCommon::FImageHandle dstHandle{
            static_cast<uint64_t>(reinterpret_cast<uintptr_t>(dst.GetImage().Handle()))};
        EmitTrackedImageBarrier(commandBuffer, viewStates, {
            .image = srcHandle,
            .stages = PipelineCommon::ERenderStage::Transfer,
            .access = PipelineCommon::EResourceAccess::TransferRead,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        }, src->GetImage().Handle(), "copy view output source");
        EmitTrackedImageBarrier(commandBuffer, outputStates, {
            .image = dstHandle,
            .stages = PipelineCommon::ERenderStage::Transfer,
            .access = PipelineCommon::EResourceAccess::TransferWrite,
            .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .discardPreviousContents = true,
        }, dst.GetImage().Handle(), "copy view output destination");

        VkImageBlit region{};
        region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.srcOffsets[0] = {0, 0, 0};
        region.srcOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
        region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.dstOffsets[0] = {0, 0, 0};
        region.dstOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
        vkCmdBlitImage(commandBuffer,
                       src->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst.GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &region, filter);

        EmitTrackedImageBarrier(commandBuffer, outputStates, {
            .image = dstHandle,
            .stages = PipelineCommon::ERenderStage::Fragment,
            .access = PipelineCommon::EResourceAccess::ShaderRead,
            .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        }, dst.GetImage().Handle(), "sample view output");
        EmitTrackedImageBarrier(commandBuffer, viewStates, {
            .image = srcHandle,
            .stages = PipelineCommon::ERenderStage::Compute,
            .access = PipelineCommon::EResourceAccess::ShaderRead | PipelineCommon::EResourceAccess::ShaderWrite,
            .layout = VK_IMAGE_LAYOUT_GENERAL,
        }, src->GetImage().Handle(), "restore view output source");
        return true;
    }

    FActiveRenderViewScope::FActiveRenderViewScope(VulkanBaseRenderer& renderer, RenderView& view)
        : renderer_(renderer)
        , previousBankBase_(renderer.activeViewBankBase_)
        , previousRenderExtent_(renderer.activeViewRenderExtent_)
        , previousCameraAddress_(renderer.activeViewCameraAddress_)
        , previousVisibilityFrameBuffer_(renderer.activeVisibilityFrameBuffer_)
        , previousSceneOverride_(renderer.activeSceneOverride_)
        , previousRenderView_(renderer.activeRenderView_)
    {
        renderer_.activeSceneOverride_ = view.SceneOverride();
        renderer_.activeRenderView_ = &view;
        renderer_.SetActiveViewBankBase(view.RtBankBase());
        renderer_.SetActiveViewRenderExtent(view.RenderExtent());
        renderer_.SetActiveViewCameraAddress(view.CameraAddress());
        renderer_.activeVisibilityFrameBuffer_ = view.VisibilityFramebuffer();
    }

    FActiveRenderViewScope::~FActiveRenderViewScope()
    {
        renderer_.activeSceneOverride_ = previousSceneOverride_;
        renderer_.activeRenderView_ = previousRenderView_;
        renderer_.activeVisibilityFrameBuffer_ = previousVisibilityFrameBuffer_;
        renderer_.SetActiveViewCameraAddress(previousCameraAddress_);
        renderer_.SetActiveViewRenderExtent(previousRenderExtent_);
        renderer_.SetActiveViewBankBase(previousBankBase_);
    }
}
