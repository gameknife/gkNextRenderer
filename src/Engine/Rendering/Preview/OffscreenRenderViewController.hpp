#pragma once

#include "Engine/Assets/Core/Model.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Rendering/RenderViewResources.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace Vulkan
{
    class FrameBuffer;
    class RenderImage;
    class RenderView;
    class Sampler;
    class VulkanBaseRenderer;

    class OffscreenRenderViewController final
    {
    public:
        static constexpr uint32_t kMaxCameraSecondaryViews = 3;
        static constexpr uint32_t kScenePreviewSecondaryViewIndex = kMaxCameraSecondaryViews;
        static constexpr uint32_t kMaxSecondaryViews = kMaxCameraSecondaryViews + 1;
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
        void RequestScenePreviewThisFrame();
        uint32_t ScenePreviewSampleSlot() const;
        bool IsScenePreviewReady() const;
        bool HasWork(bool includeDebugOverlay) const;
        void ScheduleViews(VkCommandBuffer commandBuffer, uint32_t imageIndex);
        void ClearFrameRequests();
        void OnMainSceneChanged();
        void OnSwapChainResourcesInvalidated(bool releaseSampledOutputs);

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
        void CopyViewOutput(VkCommandBuffer commandBuffer, uint32_t imageIndex, RenderView& view, uint32_t viewIndex);

        VulkanBaseRenderer& renderer_;
        std::array<FViewResources, kMaxSecondaryViews> views_{};
    };
}
