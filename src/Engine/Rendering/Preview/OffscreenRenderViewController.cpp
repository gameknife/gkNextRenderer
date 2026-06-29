#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Preview/OffscreenRenderViewController.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/RenderViewResourceFactory.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

namespace Vulkan
{
    OffscreenRenderViewController::OffscreenRenderViewController(VulkanBaseRenderer& renderer)
        : renderer_(renderer)
    {
    }

    OffscreenRenderViewController::~OffscreenRenderViewController() = default;

    void OffscreenRenderViewController::SetEnabled(uint32_t viewIndex, const bool enabled)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }
        views_[viewIndex].enabled = enabled;
    }

    bool OffscreenRenderViewController::IsEnabled(const uint32_t viewIndex) const
    {
        return viewIndex < kMaxSecondaryViews && views_[viewIndex].enabled;
    }

    void OffscreenRenderViewController::RequestThisFrame(uint32_t viewIndex)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }
        views_[viewIndex].requested = true;
    }

    void OffscreenRenderViewController::SetRenderExtent(uint32_t viewIndex, VkExtent2D extent)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }

        extent.width = std::max(1u, extent.width);
        extent.height = std::max(1u, extent.height);
        auto& resources = views_[viewIndex];
        resources.requestedExtent = extent;
        if (resources.view != nullptr)
        {
            resources.view->SetRenderExtent(extent);
        }
    }

    VkExtent2D OffscreenRenderViewController::RenderExtent(const uint32_t viewIndex) const
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return {};
        }
        return views_[viewIndex].requestedExtent;
    }

    void OffscreenRenderViewController::SetCameraOverride(uint32_t viewIndex, const Assets::Camera& camera)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }

        auto& resources = views_[viewIndex];
        const bool changed = !resources.cameraOverride.has_value() ||
            std::memcmp(&resources.cameraOverride->ModelView, &camera.ModelView, sizeof(glm::mat4)) != 0 ||
            resources.cameraOverride->FieldOfView != camera.FieldOfView ||
            resources.cameraOverride->NearPlane != camera.NearPlane ||
            resources.cameraOverride->FarPlane != camera.FarPlane;
        if (changed && resources.view != nullptr)
        {
            resources.view->InvalidateTemporalHistory();
        }
        resources.cameraOverride = camera;
    }

    void OffscreenRenderViewController::ClearCameraOverride(uint32_t viewIndex)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }
        views_[viewIndex].cameraOverride.reset();
    }

    bool OffscreenRenderViewController::HasCameraOverride(const uint32_t viewIndex) const
    {
        return viewIndex < kMaxSecondaryViews && views_[viewIndex].cameraOverride.has_value();
    }

    const Assets::UniformBufferObject* OffscreenRenderViewController::LastUniformBufferObject(
        const uint32_t viewIndex) const
    {
        if (viewIndex >= kMaxSecondaryViews || views_[viewIndex].view == nullptr)
        {
            return nullptr;
        }
        return &views_[viewIndex].view->State().previousUniformBuffer;
    }

    uint32_t OffscreenRenderViewController::SampleSlot(const uint32_t viewIndex) const
    {
        return kSecondaryViewSampleSlotBase + std::min(viewIndex, kMaxSecondaryViews - 1);
    }

    bool OffscreenRenderViewController::IsReady(const uint32_t viewIndex) const
    {
        return viewIndex < kMaxSecondaryViews && views_[viewIndex].offscreenImage != nullptr;
    }

    void OffscreenRenderViewController::RequestScenePreviewThisFrame()
    {
        RequestThisFrame(kScenePreviewSecondaryViewIndex);
    }

    uint32_t OffscreenRenderViewController::ScenePreviewSampleSlot() const
    {
        return SampleSlot(kScenePreviewSecondaryViewIndex);
    }

    bool OffscreenRenderViewController::IsScenePreviewReady() const
    {
        return IsReady(kScenePreviewSecondaryViewIndex);
    }

    bool OffscreenRenderViewController::HasWork(const bool includeDebugOverlay) const
    {
        if (includeDebugOverlay)
        {
            return true;
        }

        return std::any_of(
            views_.begin(),
            views_.end(),
            [](const FViewResources& view)
            {
                return view.enabled || view.requested;
            });
    }

    void OffscreenRenderViewController::ClearFrameRequests()
    {
        for (auto& view : views_)
        {
            view.requested = false;
        }
    }

    void OffscreenRenderViewController::OnMainSceneChanged()
    {
        for (auto& view : views_)
        {
            if (view.view != nullptr)
            {
                view.view->InvalidateTemporalHistory();
            }
        }
    }

    void OffscreenRenderViewController::OnSwapChainResourcesInvalidated(const bool releaseSampledOutputs)
    {
        for (auto& view : views_)
        {
            view.visibilityFrameBuffer.reset();
            if (releaseSampledOutputs)
            {
                view.offscreenImage.reset();
                view.offscreenSampler.reset();
            }
            if (view.view != nullptr)
            {
                view.view->ResetSwapChainResources();
            }
        }
    }

    RenderView& OffscreenRenderViewController::EnsureView(uint32_t viewIndex)
    {
        viewIndex = std::min(viewIndex, kMaxSecondaryViews - 1);
        auto& resources = views_[viewIndex];

        VkExtent2D extent = resources.requestedExtent;
        if (extent.width == 0 || extent.height == 0)
        {
            extent = renderer_.frame_.swapChain->RenderExtent();
        }
        extent.width = std::max(1u, extent.width);
        extent.height = std::max(1u, extent.height);

        if (resources.view == nullptr)
        {
            FViewDesc viewDesc{};
            viewDesc.renderExtent = extent;
            viewDesc.outputKind = EViewOutputKind::OffscreenTexture;
            viewDesc.schedule = EViewSchedule::Persistent;
            resources.view = renderer_.renderViews_->CreateView(viewDesc, fmt::format("secondary view {}", viewIndex));
            if (resources.view == nullptr)
            {
                Throw(std::runtime_error("failed to allocate secondary RenderView bank"));
            }
            resources.view->CreateSwapChain(renderer_.SwapChain());
        }
        resources.view->SetDebugName(fmt::format("secondary view {}", viewIndex));
        resources.view->SetRenderExtent(extent);
        resources.view->SetCopyObjectIdHistory(true);

        if (resources.view->AllocatedExtent().width == extent.width &&
            resources.view->AllocatedExtent().height == extent.height &&
            resources.view->VisibilityFramebuffer() != nullptr &&
            resources.offscreenImage != nullptr)
        {
            return *resources.view;
        }

        resources.visibilityFrameBuffer.reset();
        resources.offscreenImage.reset();
        RenderViewResourceFactory resourceFactory(renderer_);
        resources.visibilityFrameBuffer = resourceFactory.RebuildVisibilityFramebuffer(*resources.view, extent);

        const std::string offscreenDebugName = fmt::format("Secondary View {} Offscreen", viewIndex);
        resources.offscreenImage = resourceFactory.CreateSampledColorImage(extent, offscreenDebugName.c_str());
        if (!resources.offscreenSampler)
        {
            resources.offscreenSampler = resourceFactory.CreateClampSampler();
        }
        resourceFactory.BindSampledColorImage(
            SampleSlot(viewIndex),
            *resources.offscreenImage,
            *resources.offscreenSampler);

        return *resources.view;
    }

    void OffscreenRenderViewController::CopyViewOutput(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex,
        RenderView& view,
        uint32_t viewIndex)
    {
        viewIndex = std::min(viewIndex, kMaxSecondaryViews - 1);
        auto& resources = views_[viewIndex];
        const RenderImage* src = renderer_.GetStorageImage(view.RtBankBase() + Assets::Bindless::RT_DENOISED);
        if (!src)
        {
            return;
        }

        const VkExtent2D secondaryExtent = view.RenderExtent();
        const int32_t rw = static_cast<int32_t>(secondaryExtent.width);
        const int32_t rh = static_cast<int32_t>(secondaryExtent.height);

        src->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                           VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

        if (resources.offscreenImage)
        {
            resources.offscreenImage->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageCopy copyRegion{};
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.extent = {static_cast<uint32_t>(rw), static_cast<uint32_t>(rh), 1};
            vkCmdCopyImage(commandBuffer,
                src->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                resources.offscreenImage->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &copyRegion);
            resources.offscreenImage->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        if (renderer_.multiViewDemo_)
        {
            const int32_t sw = static_cast<int32_t>(renderer_.frame_.swapChain->Extent().width);
            const int32_t sh = static_cast<int32_t>(renderer_.frame_.swapChain->Extent().height);
            ImageMemoryBarrier::FullInsert(commandBuffer, renderer_.frame_.swapChain->Images()[imageIndex],
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
            VkImageBlit region{};
            region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.srcOffsets[0] = {0, 0, 0};
            region.srcOffsets[1] = {rw, rh, 1};
            region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            region.dstOffsets[0] = {sw / 2, sh / 2, 0};
            region.dstOffsets[1] = {sw, sh, 1};
            vkCmdBlitImage(commandBuffer,
                           src->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           renderer_.frame_.swapChain->Images()[imageIndex], VK_IMAGE_LAYOUT_GENERAL,
                           1, &region, VK_FILTER_LINEAR);
        }

        src->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void OffscreenRenderViewController::ScheduleViews(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        const auto it = renderer_.logicRenderers_.renderers.find(renderer_.logicRenderers_.current);
        if (it == renderer_.logicRenderers_.renderers.end())
        {
            return;
        }

        for (uint32_t viewIndex = 0; viewIndex < kMaxSecondaryViews; ++viewIndex)
        {
            auto& resources = views_[viewIndex];
            if (!renderer_.multiViewDemo_ && !resources.enabled && !resources.requested)
            {
                continue;
            }

            RenderView& secondaryView = EnsureView(viewIndex);
            const VkExtent2D secondaryExtent = secondaryView.RenderExtent();
            Assets::Camera viewCamera = resources.cameraOverride.has_value()
                ? *resources.cameraOverride
                : BuildOrbitedCamera(
                    renderer_.frame_.lastUBO,
                    renderer_.GetScene().GetRenderCamera(),
                    35.0f + static_cast<float>(viewIndex) * 35.0f,
                    30.0f);

            Assets::UniformBufferObject secondaryUbo = BuildViewCameraUbo({
                .scene = renderer_.GetScene(),
                .camera = viewCamera,
                .extent = secondaryExtent,
                .baseUbo = &renderer_.frame_.lastUBO,
                .cascadeDistance = 400.0f,
                .fillSceneLighting = false,
            });
            renderer_.FinalizeTemporalUbo(secondaryView, secondaryUbo);
            renderer_.SetRenderViewUbo(secondaryView, imageIndex, secondaryUbo);
            secondaryView.SetVisibilityFramebuffer(resources.visibilityFrameBuffer.get());
            secondaryView.SetSceneOverride(nullptr);
            secondaryView.SetCopyObjectIdHistory(true);

            renderer_.ScheduleRenderView(
                secondaryView,
                *it->second,
                /*clearSwapchain*/ false,
                [this, commandBuffer, imageIndex, viewIndex](RenderView& view)
                {
                    CopyViewOutput(commandBuffer, imageIndex, view, viewIndex);
                });
        }
    }
}
