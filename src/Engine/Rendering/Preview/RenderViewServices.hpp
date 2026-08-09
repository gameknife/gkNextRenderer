#pragma once

#include <vulkan/vulkan.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Vulkan
{
    class VulkanBaseRenderer;

    // A named source of auxiliary render views (thumbnails, offscreen cameras, ...)
    // scheduled by the renderer around the primary view. Implementations live in
    // modules / application layers and register themselves via RenderViewServices.
    class IRenderViewProvider
    {
    public:
        virtual ~IRenderViewProvider() = default;

        virtual void BeforeNextFrame() {}
        virtual void OnMainSceneChanged() {}
        virtual void OnHdrShUpdated() {}
        virtual void OnSwapChainResourcesInvalidated(bool releaseSampledOutputs) {}
        virtual bool HasWork() const = 0;
        // Schedule this provider's views. Return true when a transient exclusive view
        // was scheduled; lower-priority providers are then deferred to the next frame.
        virtual bool ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex) = 0;
        // Reference mode: render side-by-side comparison views. Return true if any view rendered.
        virtual bool ScheduleReferenceViews(VkCommandBuffer commandBuffer, uint32_t imageIndex) { return false; }
        virtual void ClearFrameRequests() {}
    };

    // Owns registered render-view providers for one renderer; providers are destroyed
    // with the renderer so their Vulkan resources are released deterministically.
    class RenderViewServices final
    {
    public:
        explicit RenderViewServices(VulkanBaseRenderer& renderer) : renderer_(renderer) {}

        VulkanBaseRenderer& Renderer() { return renderer_; }

        // Lower priority schedules first; an exclusive provider defers the rest.
        IRenderViewProvider* RegisterProvider(std::string_view name, int priority,
                                              std::unique_ptr<IRenderViewProvider> provider);
        IRenderViewProvider* FindProvider(std::string_view name) const;

        void BeforeNextFrame();
        void OnMainSceneChanged();
        void OnHdrShUpdated();
        void OnSwapChainResourcesInvalidated(bool releaseOffscreenSampledOutputs);
        bool HasWork() const;
        void ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        bool ScheduleReferenceViews(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void ClearOffscreenFrameRequests();

    private:
        struct FEntry
        {
            std::string name;
            int priority = 0;
            std::unique_ptr<IRenderViewProvider> provider;
        };

        VulkanBaseRenderer& renderer_;
        std::vector<FEntry> providers_;
    };
}
