#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Preview/OffscreenRenderViewController.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/RenderViewResourceFactory.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Utilities/Exception.hpp"
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
        return viewIndex < kMaxSecondaryViews && views_[viewIndex].target.offscreenImage != nullptr;
    }

    bool OffscreenRenderViewController::HasWork() const
    {
        return std::any_of(
            views_.begin(),
            views_.end(),
            [](const FViewResources& view)
            {
                return (view.enabled || view.requested) && view.cameraOverride.has_value();
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
            view.target.ResetSwapChainResources(releaseSampledOutputs);
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
            resources.target.offscreenImage != nullptr)
        {
            return *resources.view;
        }

        resources.target.visibilityFramebuffer.reset();
        resources.target.offscreenImage.reset();
        RenderViewResourceFactory resourceFactory(renderer_);
        resources.target.visibilityFramebuffer = resourceFactory.RebuildVisibilityFramebuffer(*resources.view, extent);

        const std::string offscreenDebugName = fmt::format("Secondary View {} Offscreen", viewIndex);
        resources.target.offscreenImage = resourceFactory.CreateSampledColorImage(extent, offscreenDebugName.c_str());
        if (!resources.target.offscreenSampler)
        {
            resources.target.offscreenSampler = resourceFactory.CreateClampSampler();
        }
        resources.target.outputSampleSlot = SampleSlot(viewIndex);
        resourceFactory.BindSampledColorImage(
            resources.target.outputSampleSlot,
            *resources.target.offscreenImage,
            *resources.target.offscreenSampler);

        return *resources.view;
    }

    void OffscreenRenderViewController::CopyViewOutput(
        VkCommandBuffer commandBuffer,
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

        if (resources.target.offscreenImage)
        {
            resources.target.offscreenImage->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageCopy copyRegion{};
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.extent = {static_cast<uint32_t>(rw), static_cast<uint32_t>(rh), 1};
            vkCmdCopyImage(commandBuffer,
                src->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                resources.target.offscreenImage->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &copyRegion);
            resources.target.offscreenImage->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        src->InsertBarrier(commandBuffer, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void OffscreenRenderViewController::ScheduleViews(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        LogicRendererBase* logicRenderer = renderer_.EnsureLogicRenderer(renderer_.logicRenderers_.current);
        if (logicRenderer == nullptr)
        {
            return;
        }

        for (uint32_t viewIndex = 0; viewIndex < kMaxSecondaryViews; ++viewIndex)
        {
            auto& resources = views_[viewIndex];
            if ((!resources.enabled && !resources.requested) || !resources.cameraOverride.has_value())
            {
                continue;
            }

            RenderView& secondaryView = EnsureView(viewIndex);
            const VkExtent2D secondaryExtent = secondaryView.RenderExtent();
            const Assets::Camera& viewCamera = *resources.cameraOverride;

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
            secondaryView.SetVisibilityFramebuffer(resources.target.visibilityFramebuffer.get());
            secondaryView.SetSceneOverride(nullptr);
            secondaryView.SetCopyObjectIdHistory(true);

            renderer_.ScheduleRenderView(
                secondaryView,
                *logicRenderer,
                /*clearSwapchain*/ false,
                [this, commandBuffer, imageIndex, viewIndex](RenderView& view)
                {
                    CopyViewOutput(commandBuffer, view, viewIndex);
                    if (viewRenderedCallback_)
                    {
                        viewRenderedCallback_(viewIndex, commandBuffer, imageIndex, view);
                    }
                });
        }
    }
}
