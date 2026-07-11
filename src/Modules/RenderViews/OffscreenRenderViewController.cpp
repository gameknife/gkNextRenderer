#include "Engine/Common/CoreMinimal.hpp"
#include "Modules/RenderViews/OffscreenRenderViewController.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Rendering/ViewCameraUboBuilder.hpp"
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
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

    OffscreenRenderViewController::~OffscreenRenderViewController()
    {
        RenderViewResourceFactory factory(renderer_);
        for (auto& resources : views_)
        {
            factory.DestroyView(resources.view);
        }
        for (auto& [type, resources] : referenceViews_)
        {
            (void)type;
            factory.DestroyView(resources.view);
        }
    }

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
        if (RenderView* view = Resolve(resources))
        {
            view->SetRenderExtent(extent);
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
        if (!resources.cameraOverride.has_value())
        {
            if (RenderView* view = Resolve(resources)) view->InvalidateTemporalHistory();
        }
        resources.cameraOverride = camera;
    }

    void OffscreenRenderViewController::ClearCameraOverride(uint32_t viewIndex)
    {
        if (viewIndex >= kMaxSecondaryViews)
        {
            return;
        }
        if (views_[viewIndex].cameraOverride.has_value())
        {
            if (RenderView* view = Resolve(views_[viewIndex])) view->InvalidateTemporalHistory();
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
        if (viewIndex >= kMaxSecondaryViews)
        {
            return nullptr;
        }
        const RenderView* view = Resolve(views_[viewIndex]);
        return view ? &view->State().previousUniformBuffer : nullptr;
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
            if (RenderView* resolved = Resolve(view))
            {
                resolved->InvalidateTemporalHistory();
            }
        }
        for (auto& [rendererType, view] : referenceViews_)
        {
            if (RenderView* resolved = Resolve(view))
            {
                resolved->InvalidateTemporalHistory();
                resolved->SetSceneOverride(nullptr);
            }
        }
    }

    void OffscreenRenderViewController::OnSwapChainResourcesInvalidated(const bool releaseSampledOutputs)
    {
        for (auto& view : views_)
        {
            view.target.ResetSwapChainResources(releaseSampledOutputs);
        }
        for (auto& [rendererType, view] : referenceViews_)
        {
            view.target.ResetSwapChainResources(/*releaseSampledOutput*/ false);
            if (RenderView* resolved = Resolve(view))
            {
                resolved->SetSceneOverride(nullptr);
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
            extent = renderer_.SwapChain().RenderExtent();
        }
        extent.width = std::max(1u, extent.width);
        extent.height = std::max(1u, extent.height);

        RenderViewResourceFactory resourceFactory(renderer_);
        FViewDesc viewDesc{};
        viewDesc.renderExtent = extent;
        viewDesc.outputKind = EViewOutputKind::OffscreenTexture;
        viewDesc.schedule = EViewSchedule::Persistent;
        RenderView& view = resourceFactory.EnsureView(
            resources.view,
            viewDesc,
            fmt::format("secondary view {}", viewIndex),
            true);
        const std::string offscreenDebugName = fmt::format("Secondary View {} Offscreen", viewIndex);
        resourceFactory.EnsureSampledOffscreenTarget(
            view,
            resources.target,
            extent,
            SampleSlot(viewIndex),
            offscreenDebugName.c_str());

        return view;
    }

    RenderView* OffscreenRenderViewController::Resolve(const FViewResources& resources) const
    {
        return renderer_.RenderViews().Resolve(resources.view);
    }

    RenderView& OffscreenRenderViewController::EnsureReferenceView(
        const int rendererType,
        const uint32_t imageIndex)
    {
        const auto type = static_cast<ERendererType>(rendererType);
        const FReferenceViewLayout layout = GetReferenceViewLayout(type);
        VkExtent2D extent{
            std::max(1u, renderer_.SwapChain().RenderExtent().width / 2u),
            std::max(1u, renderer_.SwapChain().RenderExtent().height / 2u)};
        VkOffset2D offset{
            static_cast<int32_t>(layout.column * extent.width),
            static_cast<int32_t>(layout.row * extent.height)};

        auto& resources = referenceViews_[rendererType];
        FViewDesc viewDesc{};
        viewDesc.renderExtent = extent;
        viewDesc.outputKind = EViewOutputKind::SwapchainSubrect;
        viewDesc.schedule = EViewSchedule::Persistent;
        viewDesc.subrect = VkRect2D{offset, extent};
        RenderViewResourceFactory resourceFactory(renderer_);
        RenderView& view = resourceFactory.EnsureView(resources.view, viewDesc, layout.debugName, true);
        view.SetRenderOffset({0, 0});
        view.SetSceneOverride(nullptr);

        if (view.AllocatedExtent().width != extent.width ||
            view.AllocatedExtent().height != extent.height)
        {
            resources.target.visibilityFramebuffer.reset();
            resources.target.visibilityFramebuffer = resourceFactory.RebuildVisibilityFramebuffer(view, extent);
        }

        Assets::UniformBufferObject ubo = renderer_.LastUniformBufferObject();
        ubo.ViewportRect = glm::vec4(0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height));
        ubo.Jitter = glm::vec4(0.0f);
        ubo.TemporalFrames = 1;
        ubo.TAA = false;
        ubo.ProgressiveRender = false;
        renderer_.FinalizeTemporalUbo(view, ubo);

        renderer_.SetRenderViewUbo(view, imageIndex, ubo);
        view.SetVisibilityFramebuffer(resources.target.visibilityFramebuffer.get());
        return view;
    }

    void OffscreenRenderViewController::CopyViewOutput(
        VkCommandBuffer commandBuffer,
        RenderView& view,
        uint32_t viewIndex)
    {
        viewIndex = std::min(viewIndex, kMaxSecondaryViews - 1);
        auto& resources = views_[viewIndex];
        if (resources.target.offscreenImage)
        {
            RenderViewResourceFactory(renderer_).CopyDenoisedOutputToImage(
                commandBuffer,
                view,
                *resources.target.offscreenImage,
                VK_FILTER_NEAREST);
        }
    }

    void OffscreenRenderViewController::ReleaseReferenceViews()
    {
        RenderViewResourceFactory factory(renderer_);
        for (auto& [type, resources] : referenceViews_)
        {
            (void)type;
            factory.DestroyView(resources.view);
        }
        referenceViews_.clear();
    }

    void OffscreenRenderViewController::ReleaseSecondaryViews()
    {
        RenderViewResourceFactory factory(renderer_);
        for (auto& resources : views_)
        {
            factory.DestroyView(resources.view);
        }
    }

    bool OffscreenRenderViewController::ScheduleViews(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        ReleaseReferenceViews();
        if (!HasWork())
        {
            return false;
        }

        LogicRendererBase* logicRenderer = renderer_.EnsureLogicRenderer(renderer_.CurrentLogicRendererType());
        if (logicRenderer == nullptr)
        {
            return false;
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
                .baseUbo = &renderer_.LastUniformBufferObject(),
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
        return false;
    }

    bool OffscreenRenderViewController::ScheduleReferenceViews(
        VkCommandBuffer commandBuffer,
        const uint32_t imageIndex)
    {
        ReleaseSecondaryViews();
        bool clearSwapchain = true;
        bool renderedAny = false;
        for (const ERendererType rendererType : GetReferenceRendererTypes())
        {
            LogicRendererBase* logicRenderer = renderer_.EnsureLogicRenderer(rendererType);
            if (logicRenderer == nullptr)
            {
                continue;
            }

            RenderView& referenceView = EnsureReferenceView(static_cast<int>(rendererType), imageIndex);
            renderer_.ScheduleRenderView(
                referenceView,
                *logicRenderer,
                clearSwapchain,
                [this, commandBuffer, imageIndex](RenderView& view)
                {
                    renderer_.ComposeViewToSwapchainSubrect(commandBuffer, imageIndex, view);
                });
            clearSwapchain = false;
            renderedAny = true;
        }

        return renderedAny;
    }
}

namespace RenderViews
{
    Vulkan::OffscreenRenderViewController& OffscreenViews(Vulkan::VulkanBaseRenderer& renderer)
    {
        Vulkan::RenderViewServices& services = renderer.ViewServices();
        if (Vulkan::IRenderViewProvider* provider = services.FindProvider("OffscreenViews"))
        {
            return static_cast<Vulkan::OffscreenRenderViewController&>(*provider);
        }
        return static_cast<Vulkan::OffscreenRenderViewController&>(*services.RegisterProvider(
            "OffscreenViews", 10, std::make_unique<Vulkan::OffscreenRenderViewController>(renderer)));
    }
}
