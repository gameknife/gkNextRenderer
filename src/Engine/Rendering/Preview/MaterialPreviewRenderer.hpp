#pragma once

#include "Engine/Assets/Data/Material.hpp"
#include "Engine/Rendering/RenderViewResources.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

namespace Assets
{
    class Scene;
}

namespace Vulkan
{
    class RenderView;
    class RenderImage;
    class VulkanBaseRenderer;

    class MaterialPreviewRenderer final
    {
    public:
        static constexpr uint32_t kMaterialPreviewSampleSlot = 64600;

        explicit MaterialPreviewRenderer(VulkanBaseRenderer& renderer);
        ~MaterialPreviewRenderer();

        void SetEnabled(bool enabled);
        bool IsEnabled() const { return enabled_; }
        void SetRenderExtent(VkExtent2D extent);
        VkExtent2D RenderExtent() const { return requestedExtent_; }
        void SetPreviewMaterial(const Assets::FMaterial& material);
        void SetCameraOrbit(float yawRadians, float pitchRadians, float distance);
        uint32_t SampleSlot() const { return kMaterialPreviewSampleSlot; }
        bool IsReady() const { return target_.offscreenImage != nullptr; }
        bool HasWork() const { return enabled_; }

        void BeforeNextFrame();
        void OnMainSceneChanged();
        void OnHdrShUpdated();
        void OnSwapChainResourcesInvalidated();
        bool ScheduleView(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    private:
        void EnsurePreviewScene();
        void EnsureRenderTarget();
        void CopyPreviewOutput(VkCommandBuffer commandBuffer, RenderView& view);

        VulkanBaseRenderer& renderer_;
        RenderView* previewView_ = nullptr;
        std::unique_ptr<Assets::Scene> previewScene_;
        FRenderViewTargetResources target_;
        VkExtent2D requestedExtent_{256, 256};
        bool enabled_ = false;
        bool sceneReady_ = false;
        bool materialDirty_ = true;
        float cameraYaw_ = 0.0f;
        float cameraPitch_ = 0.0f;
        float cameraDistance_ = 4.0f;
        Assets::FMaterial previewMaterial_{};
    };
}
