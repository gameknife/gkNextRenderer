#pragma once
#include "Engine/Common/CoreMinimal.hpp" // GK_NON_COPIABLE
#include "Engine/Assets/AssetsFwd.hpp"
#include "Engine/Runtime/RuntimeFwd.hpp"
#include "Engine/Runtime/Editor/MultiViewportBackend.hpp"
#include "Engine/Runtime/Editor/UiFrame.hpp"
#include "Engine/Runtime/Editor/UiTextureResolver.hpp"
#include "Engine/Vulkan/VulkanFwd.hpp"
#include <imgui.h>
#include <vulkan/vulkan.h>
#include <deque>
#include <glm/vec4.hpp>

union SDL_Event;

namespace NextUI
{
class FImGuiContextHost;
class FImGuiVulkanRenderer;

enum class EUiTextureLifetime
{
    Transient,
    Persistent,
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

    mutable std::unordered_map< std::string, std::string> Stats;
};

class UserInterface final
{
public:

    GK_NON_COPIABLE(UserInterface)

    UserInterface(
        NextEngine* engine,
        Vulkan::CommandPool& commandPool, 
        const Vulkan::SwapChain& swapChain, 
        const Vulkan::DepthBuffer& depthBuffer,
        Runtime::Config::UserSettings& userSettings,
        std::function<void()> funcPreConfig,
        std::function<void()> funcInit,
        std::unique_ptr<IMultiViewportBackend> multiViewportBackend);
    ~UserInterface();

    void PreRender();
    void PrepareDrawData();
    void RenderPreparedDrawData(VkCommandBuffer commandBuffer, const Vulkan::SwapChain& swapChain, uint32_t imageIdx,
                                bool suppressAllUi = false);
    void HandleEvent(const SDL_Event* event);

    bool WantsToCaptureKeyboard() const;
    bool WantsToCaptureMouse() const;

    Runtime::Config::UserSettings& Settings() { return userSettings_; }

    void OnCreateSurface(const Vulkan::SwapChain& swapChain, 
        const Vulkan::DepthBuffer& depthBuffer);
    void OnDestroySurface();

    ImTextureID RequestImTextureId(uint32_t globalTextureId);
    // Like RequestImTextureId but for an explicitly-bound bindless sample slot that is NOT a
    // registered TextureImage (e.g. a render-view offscreen output bound via BindSampleTexture).
    ImTextureID RequestImTextureIdRaw(uint32_t bindlessSampleSlot) { return EncodeBindlessTextureId(bindlessSampleSlot); }
    ImTextureID RequestImTextureIdRawOutput(uint32_t bindlessSampleSlot);
    ImTextureID RequestImTextureByName(const std::string& name);

    using FUiTextureHandle = NextUI::FUiTextureHandle;
    FUiTextureHandle RequestUiTexture(const std::string& path, bool srgb = true,
                                      EUiTextureLifetime lifetime = EUiTextureLifetime::Transient);
    
    void DrawPoint(float x, float y, float size, glm::vec4 color);
    void DrawLine(float fromx, float fromy,float tox, float toy, float size, glm::vec4 color);

    VkPipeline CreateViewportPipeline(VkRenderPass renderPass) const;
    void DestroyViewportPipeline(VkPipeline pipeline) const;
    void RenderViewportDrawData(ImDrawData* drawData, VkCommandBuffer commandBuffer, UiRenderBuffer& renderBuffer,
                                VkExtent2D framebufferExtent, uint32_t hdrOutputMode, VkPipeline pipeline);
    ImFontAtlas* GetFontAtlas() const;
    ImFont* GetDefaultFont() const;
    ImFont* GetTitleBarFont() const { return titleBarFont_; }
    float UiScale() const { return uiScale_; }
    void AttachRendererBackendToCurrentContext() const;

private:
    NextEngine& GetEngine() {return *engine_;}

    void DrawIndicator(uint32_t frameCount, bool show);
    void InitializeRendererBackend();
    void ShutdownRendererBackend();
    void BeginRendererBackendFrame();
    void CreateUiPipeline(const Vulkan::SwapChain& swapChain);
    void DestroyUiPipeline();
    void InitializeFontTexture(Vulkan::CommandPool& commandPool);
    void RenderDrawData(ImDrawData* drawData, VkCommandBuffer commandBuffer, UiRenderBuffer& renderBuffers,
                        VkExtent2D framebufferExtent, uint32_t hdrOutputMode, VkPipeline pipeline);
    static ImTextureID EncodeBindlessTextureId(uint32_t textureIndex, uint32_t textureFlags = 0);
    static bool DecodeBindlessTextureId(ImTextureID textureId, uint32_t& outTextureIndex, uint32_t& outTextureFlags);

    std::unique_ptr<Vulkan::RenderPass> renderPass_;
    std::string imguiIniPath_;
    std::vector< Vulkan::FrameBuffer > uiFrameBuffers_;
    std::vector<UiRenderBuffer> uiRenderBuffers_;
    VkPipelineLayout uiPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline uiPipeline_ = VK_NULL_HANDLE;
    Runtime::Config::UserSettings& userSettings_;
    
    std::unique_ptr<IMultiViewportBackend> multiViewportBackend_;
    std::unique_ptr<FUiTextureResolver> textureResolver_;
    std::unique_ptr<FImGuiContextHost> contextHost_;
    std::unique_ptr<FImGuiVulkanRenderer> vulkanRenderer_;
    ImFontAtlas* fontAtlas_ = nullptr;
    ImFont* defaultFont_ = nullptr;
    ImFont* titleBarFont_ = nullptr;
    float uiScale_ = 1.0f;
    uint32_t fontTextureIndex_ = UINT32_MAX;
    std::vector< std::function<void ()> > auxDrawRequest_;
    bool hasPreparedDrawData_ = false;
    double loadingStartedAt_ = -1.0;
    bool loadingIndicatorOpen_ = false;
    NextEngine* engine_;
};

}
