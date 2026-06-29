#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Rendering/Preview/ReferenceRenderViewController.hpp"

#include "Engine/Rendering/RenderViewResourceFactory.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

namespace Vulkan
{
    ReferenceRenderViewController::ReferenceRenderViewController(VulkanBaseRenderer& renderer)
        : renderer_(renderer)
    {
    }

    ReferenceRenderViewController::~ReferenceRenderViewController() = default;

    void ReferenceRenderViewController::OnMainSceneChanged()
    {
        for (auto& view : views_)
        {
            if (view.second.view != nullptr)
            {
                view.second.view->InvalidateTemporalHistory();
                view.second.view->SetSceneOverride(nullptr);
            }
        }
    }

    void ReferenceRenderViewController::OnSwapChainResourcesInvalidated()
    {
        for (auto& view : views_)
        {
            auto& resources = view.second;
            resources.target.ResetSwapChainResources(/*releaseSampledOutput*/ false);
            if (resources.view != nullptr)
            {
                resources.view->SetSceneOverride(nullptr);
            }
        }
    }

    RenderView& ReferenceRenderViewController::EnsureView(const ERendererType type, const uint32_t imageIndex)
    {
        const FReferenceViewLayout layout = GetReferenceViewLayout(type);
        VkExtent2D extent{
            std::max(1u, renderer_.frame_.swapChain->RenderExtent().width / 2u),
            std::max(1u, renderer_.frame_.swapChain->RenderExtent().height / 2u)};
        VkOffset2D offset{
            static_cast<int32_t>(layout.column * extent.width),
            static_cast<int32_t>(layout.row * extent.height)};

        auto& resources = views_[type];
        if (resources.view == nullptr)
        {
            FViewDesc viewDesc{};
            viewDesc.renderExtent = extent;
            viewDesc.outputKind = EViewOutputKind::SwapchainSubrect;
            viewDesc.schedule = EViewSchedule::Persistent;
            viewDesc.subrect = VkRect2D{offset, extent};
            resources.view = renderer_.renderViews_->CreateView(viewDesc, layout.debugName);
            if (resources.view == nullptr)
            {
                Throw(std::runtime_error("failed to allocate reference RenderView bank"));
            }
            resources.view->CreateSwapChain(renderer_.SwapChain());
        }

        RenderView& view = *resources.view;
        view.SetDebugName(layout.debugName);
        view.SetRenderExtent(extent);
        view.SetRenderOffset({0, 0});
        view.SetSubrect(VkRect2D{offset, extent});
        view.SetSceneOverride(nullptr);
        view.SetCopyObjectIdHistory(true);

        if (view.AllocatedExtent().width != extent.width ||
            view.AllocatedExtent().height != extent.height)
        {
            resources.target.visibilityFramebuffer.reset();
            RenderViewResourceFactory resourceFactory(renderer_);
            resources.target.visibilityFramebuffer = resourceFactory.RebuildVisibilityFramebuffer(view, extent);
        }

        Assets::UniformBufferObject ubo = renderer_.frame_.lastUBO;
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

    bool ReferenceRenderViewController::ScheduleViews(VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        static constexpr std::array<ERendererType, 4> kReferenceRendererTypes{
            ERT_SoftwareModern,
            ERT_SoftwareTracing,
            ERT_SoftwareModernNoAmbient,
            ERT_PathTracing,
        };

        bool clearSwapchain = true;
        bool renderedAny = false;
        for (const ERendererType rendererType : kReferenceRendererTypes)
        {
            const auto logicRenderer = renderer_.logicRenderers_.renderers.find(rendererType);
            if (logicRenderer == renderer_.logicRenderers_.renderers.end())
            {
                continue;
            }

            RenderView& referenceView = EnsureView(rendererType, imageIndex);
            renderer_.ScheduleRenderView(
                referenceView,
                *logicRenderer->second,
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
