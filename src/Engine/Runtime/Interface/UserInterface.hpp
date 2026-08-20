#pragma once

#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"

#include <glm/vec4.hpp>
#include <imgui.h>
#include <vulkan/vulkan.h>

union SDL_Event;

namespace NextUI
{
    class UiRenderBuffer;

    enum class EUiTextureLifetime
    {
        Transient,
        Persistent,
    };

    struct FUiTextureHandle
    {
        ImTextureID textureId = 0;
        ImVec2 pixelSize{0.0f, 0.0f};
        bool valid = false;
    };

    struct Statistics final
    {
        VkExtent2D FramebufferSize;
        VkExtent2D RenderSize;
        float FrameRate;
        float FrameTime;
        float RayRate;
        uint32_t TotalSamples;
        uint32_t TotalFrames;
        double RenderTime;
        uint32_t TriCount;
        uint32_t InstanceCount;
        uint32_t NodeCount;
        uint32_t TextureCount;
        uint32_t ComputePassCount;
        bool LoadingStatus;
        mutable std::unordered_map<std::string, std::string> Stats;
    };

    // Stable runtime-facing UI contract. The ImGui/Vulkan implementation lives in
    // Modules/NextUI and is installed explicitly by applications that need it.
    class IUserInterface
    {
    public:
        using FUiTextureHandle = NextUI::FUiTextureHandle;

        virtual ~IUserInterface() = default;

        virtual void PreRender() = 0;
        virtual void PrepareDrawData() = 0;
        virtual void RenderPreparedDrawData(VkCommandBuffer commandBuffer,
                                            const Vulkan::SwapChain& swapChain,
                                            uint32_t imageIdx,
                                            bool suppressAllUi = false) = 0;
        virtual void HandleEvent(const SDL_Event* event) = 0;

        virtual bool WantsToCaptureKeyboard() const = 0;
        virtual bool WantsToCaptureMouse() const = 0;
        virtual Runtime::Config::UserSettings& Settings() = 0;

        virtual void OnCreateSurface(const Vulkan::SwapChain& swapChain,
                                     const Vulkan::DepthBuffer& depthBuffer) = 0;
        virtual void OnDestroySurface() = 0;

        virtual ImTextureID RequestImTextureId(uint32_t globalTextureId) = 0;
        virtual ImTextureID RequestImTextureIdRaw(uint32_t bindlessSampleSlot) = 0;
        virtual ImTextureID RequestImTextureIdRawOutput(uint32_t bindlessSampleSlot) = 0;
        virtual ImTextureID RequestImTextureByName(const std::string& name) = 0;
        virtual FUiTextureHandle RequestUiTexture(
            const std::string& path,
            bool srgb = true,
            EUiTextureLifetime lifetime = EUiTextureLifetime::Transient) = 0;

        virtual void DrawPoint(float x, float y, float size, glm::vec4 color) = 0;
        virtual void DrawLine(float fromX, float fromY, float toX, float toY, float size, glm::vec4 color) = 0;

        virtual VkPipeline CreateViewportPipeline(VkRenderPass renderPass) const = 0;
        virtual void DestroyViewportPipeline(VkPipeline pipeline) const = 0;
        virtual void RenderViewportDrawData(ImDrawData* drawData,
                                            VkCommandBuffer commandBuffer,
                                            UiRenderBuffer& renderBuffer,
                                            VkExtent2D framebufferExtent,
                                            uint32_t hdrOutputMode,
                                            VkPipeline pipeline) = 0;
        virtual ImFontAtlas* GetFontAtlas() const = 0;
        virtual ImFont* GetDefaultFont() const = 0;
        virtual ImFont* GetTitleBarFont() const = 0;
        virtual float UiScale() const = 0;
        virtual void AttachRendererBackendToCurrentContext() const = 0;
    };
}
