#pragma once

#include "Engine/Rendering/VulkanBaseRenderer.hpp"

#include <map>
#include <memory>

namespace Vulkan
{
    class FrameBuffer;
    class RenderView;
    class VulkanBaseRenderer;

    class ReferenceRenderViewController final
    {
    public:
        explicit ReferenceRenderViewController(VulkanBaseRenderer& renderer);
        ~ReferenceRenderViewController();

        void OnMainSceneChanged();
        void OnSwapChainResourcesInvalidated();
        bool ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    private:
        struct FViewResources
        {
            RenderView* view = nullptr;
            std::unique_ptr<FrameBuffer> visibilityFrameBuffer;
        };

        RenderView& EnsureView(ERendererType type, uint32_t imageIndex);

        VulkanBaseRenderer& renderer_;
        std::map<ERendererType, FViewResources> views_;
    };
}
