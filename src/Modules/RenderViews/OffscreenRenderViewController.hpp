#pragma once

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/RenderView.hpp"
#include "Engine/Rendering/Preview/RenderViewServices.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace Vulkan
{
    class FrameBuffer;
    class RenderImage;
    class RenderView;
    class Sampler;
    class VulkanBaseRenderer;

    class OffscreenRenderViewController final : public IRenderViewProvider
    {
    public:
        using FViewRenderedCallback =
            std::function<void(uint32_t viewIndex, VkCommandBuffer commandBuffer, uint32_t imageIndex, RenderView& view)>;

        static constexpr uint32_t kMaxCameraSecondaryViews = 3;
        static constexpr uint32_t kMaxSecondaryViews = kMaxCameraSecondaryViews;
        static constexpr uint32_t kSecondaryViewSampleSlotBase = 65000;

        explicit OffscreenRenderViewController(VulkanBaseRenderer& renderer);
        ~OffscreenRenderViewController();

        void SetEnabled(uint32_t viewIndex, bool enabled);
        bool IsEnabled(uint32_t viewIndex) const;
        void RequestThisFrame(uint32_t viewIndex);
        void SetRenderExtent(uint32_t viewIndex, VkExtent2D extent);
        VkExtent2D RenderExtent(uint32_t viewIndex) const;
        void SetCameraOverride(uint32_t viewIndex, const Assets::Camera& camera);
        void ClearCameraOverride(uint32_t viewIndex);
        bool HasCameraOverride(uint32_t viewIndex) const;
        const Assets::UniformBufferObject* LastUniformBufferObject(uint32_t viewIndex) const;
        uint32_t SampleSlot(uint32_t viewIndex) const;
        bool IsReady(uint32_t viewIndex) const;
        bool HasWork() const override;
        bool ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
        bool ScheduleReferenceViews(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;
        void ClearFrameRequests() override;
        void OnMainSceneChanged() override;
        void OnSwapChainResourcesInvalidated(bool releaseSampledOutputs) override;
        void SetViewRenderedCallback(FViewRenderedCallback callback) { viewRenderedCallback_ = std::move(callback); }

    private:
        struct FViewResources
        {
            RenderView* view = nullptr;
            bool enabled = false;
            bool requested = false;
            VkExtent2D requestedExtent{0, 0};
            std::optional<Assets::Camera> cameraOverride{};
            FRenderViewTargetResources target;
        };

        RenderView& EnsureView(uint32_t viewIndex);
        RenderView& EnsureReferenceView(int rendererType, uint32_t imageIndex);
        void CopyViewOutput(VkCommandBuffer commandBuffer, RenderView& view, uint32_t viewIndex);

        VulkanBaseRenderer& renderer_;
        std::array<FViewResources, kMaxSecondaryViews> views_{};
        std::map<int, FViewResources> referenceViews_;
        FViewRenderedCallback viewRenderedCallback_{};
    };
}

namespace RenderViews
{
    // Get-or-create the shared offscreen-view provider on this renderer
    // (used by the editor camera view panel and NextRemote streaming).
    Vulkan::OffscreenRenderViewController& OffscreenViews(Vulkan::VulkanBaseRenderer& renderer);
}
