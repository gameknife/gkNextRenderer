#pragma once

#include "Engine/Rendering/VulkanBaseRenderer.hpp"

#include <array>
#include <memory>
#include <vector>

namespace Vulkan::Compatibility
{
    // The renderer for devices that cannot meet the full renderer's contract -- MoltenVK on
    // A12X-class GPUs, which have neither the descriptor budget for the bindless arrays nor
    // bufferDeviceAddress. Its contract declares no outputs, so the base renderer skips the whole
    // screen-space chain (RT banks, visibility buffer, shared compute pipelines) and this class
    // owns the swapchain image directly.
    //
    // What it draws: one raster pass fills a deliberately small G-buffer (base colour + normal),
    // then an explicitly-bound compute shader applies the preview lighting and blits its output to
    // the swapchain. The entire mini chain is owned here rather than by the full screen-space RT
    // bank, which this hardware cannot create.
    //
    // The two resource rules that define this path, both forced by the target hardware:
    //   * no bindless descriptor set -- it binds its own small set of five storage buffers;
    //   * no buffer device addresses -- the GPUScene push constant is entirely made of them, so
    //     this pass uses its own push constant (camera matrix + proxy index) instead.
    //
    // What it deliberately does not do (see the design doc for why each is deferred): no GPU cull /
    // soft-mesh expansion (those compute passes bind the bindless set), no skinning, no albedo
    // textures, no shadows, no visibility IDs, no frustum culling.
    // docs/designs/ios-a12x-compatibility-minimal-render-mvp.md
    class CompatibilityRenderer final : public LogicRendererBase
    {
    public:
        // Matches CompatibilityDrawPushConstants in Rast.CompatibilityAlbedo.vert.slang. It stays
        // free of GPUScene addresses: that type cannot even be constructed on the target hardware.
        struct FDrawPushConstants
        {
            glm::mat4 ViewProjection;
            uint32_t ProxyIndex;
        };

        // Matches CompatibilityShadePushConstants in Core.CompatibilityShade.comp.slang. The
        // preview deliberately uses light hues, not scene radiance, because it cannot sample the
        // sky IBL used to calibrate the full renderer's intensity values.
        struct FShadePushConstants
        {
            glm::vec4 SunDirection;
            glm::vec4 SunColor;
            glm::vec4 SkyColor;
        };

        // Storage-buffer bindings of set 0, mirroring the shader declarations. EB_VertexWords is
        // the vertex buffer read as raw uints -- see the shader for why it cannot be typed.
        enum EBinding : uint32_t
        {
            EB_Nodes = 0,
            EB_VertexWords = 1,
            EB_Indices = 2,
            EB_Offsets = 3,
            EB_Materials = 4,
            EB_Count,
        };

        enum EShadeBinding : uint32_t
        {
            ESB_Albedo = 0,
            ESB_Normal = 1,
            ESB_SceneColor = 2,
            ESB_Count,
        };

        explicit CompatibilityRenderer(VulkanBaseRenderer& baseRender) : LogicRendererBase(baseRender) {}
        ~CompatibilityRenderer() override;

        void CreateSwapChain(const VkExtent2D& extent) override;
        void DeleteSwapChain() override;
        void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

    private:
        // Re-points set 0 at the current scene's buffers. Cheap and idempotent: it compares the
        // bound handles first, so a scene reload rewrites the descriptors and a steady frame does
        // nothing.
        void BindSceneBuffers(const Assets::Scene& scene);
        void TransitionGBufferForRaster(VkCommandBuffer commandBuffer);
        void TransitionGBufferForShading(VkCommandBuffer commandBuffer);
        void TransitionSceneColorForShading(VkCommandBuffer commandBuffer);

        std::unique_ptr<class RenderImage> gbufferAlbedo_;
        std::unique_ptr<class RenderImage> gbufferNormal_;
        std::unique_ptr<class RenderImage> sceneColor_;
        std::unique_ptr<class RenderPass> gbufferRenderPass_;
        std::unique_ptr<class FrameBuffer> gbufferFrameBuffer_;
        std::unique_ptr<class PipelineLayout> drawPipelineLayout_;
        std::unique_ptr<class DescriptorSetManager> drawDescriptorSetManager_;
        std::unique_ptr<class PipelineLayout> shadePipelineLayout_;
        std::unique_ptr<class DescriptorSetManager> shadeDescriptorSetManager_;
        std::array<VkBuffer, EB_Count> boundBuffers_{};
        VkPipeline drawPipeline_ = VK_NULL_HANDLE;
        VkPipeline shadePipeline_ = VK_NULL_HANDLE;
        bool gbufferInitialized_ = false;
        bool sceneColorInitialized_ = false;
    };
}
