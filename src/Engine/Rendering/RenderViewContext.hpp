#pragma once

#include "Engine/Vulkan/VulkanFwd.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Assets
{
    class Scene;
}

namespace Vulkan
{
    class FrameBuffer;
    class RenderView;
    class VulkanBaseRenderer;

    class FActiveRenderViewScope final
    {
    public:
        FActiveRenderViewScope(VulkanBaseRenderer& renderer, RenderView& view);
        ~FActiveRenderViewScope();

        FActiveRenderViewScope(const FActiveRenderViewScope&) = delete;
        FActiveRenderViewScope& operator=(const FActiveRenderViewScope&) = delete;

    private:
        VulkanBaseRenderer& renderer_;
        uint32_t previousBankBase_ = 0;
        VkExtent2D previousRenderExtent_{0, 0};
        VkDeviceAddress previousCameraAddress_ = 0;
        FrameBuffer* previousVisibilityFrameBuffer_ = nullptr;
        Assets::Scene* previousSceneOverride_ = nullptr;
        RenderView* previousRenderView_ = nullptr;
    };
}
