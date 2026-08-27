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
    // What it draws: one raster pass, clearing to a background colour and filling every render
    // proxy with its material base-colour factor.
    //
    // The two resource rules that define this path, both forced by the target hardware:
    //   * no bindless descriptor set -- it binds its own small set of five storage buffers;
    //   * no buffer device addresses -- the GPUScene push constant is entirely made of them, so
    //     this pass uses its own push constant (camera matrix + proxy index) instead.
    //
    // What it deliberately does not do (see the design doc for why each is deferred): no GPU cull /
    // soft-mesh expansion (those compute passes bind the bindless set), no skinning, no albedo
    // textures, no lighting, no shadows, no visibility IDs, no frustum culling.
    // docs/designs/ios-a12x-compatibility-minimal-render-mvp.md
    class CompatibilityRenderer final : public LogicRendererBase
    {
    public:
        // Matches CompatibilityPushConstants in Rast.CompatibilityAlbedo.{vert,frag}.slang.
        // 116 bytes, padded to the 128-byte push-constant floor the engine already targets.
        struct FPushConstants
        {
            glm::mat4 ViewProjection;
            glm::vec4 SunDirection;
            // rgb from UniformBufferObject; w carries HasSun / HasSky so the shader needs no branch.
            // Intensities are deliberately not folded in -- see the fragment shader.
            glm::vec4 SunColor;
            glm::vec4 SkyColor;
            uint32_t ProxyIndex;
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

        std::unique_ptr<class RenderPass> renderPass_;
        std::unique_ptr<class PipelineLayout> pipelineLayout_;
        std::unique_ptr<class DescriptorSetManager> descriptorSetManager_;
        std::vector<class FrameBuffer> frameBuffers_;
        std::array<VkBuffer, EB_Count> boundBuffers_{};
        VkPipeline pipeline_ = VK_NULL_HANDLE;
    };
}
