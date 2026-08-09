#pragma once

#include "Engine/Vulkan/DebugUtilities.hpp"
#include <vector>

namespace Vulkan
{
    class Device;
    class ShaderModule;

    // Chained builder over VkGraphicsPipelineCreateInfo with the engine's
    // common defaults: triangle list, fill, no culling, CCW front face, no
    // MSAA, depth off, one opaque RGBA color attachment, fixed viewport.
    // Callers override only what differs, then call Build().
    class GraphicsPipelineBuilder final
    {
    public:
        explicit GraphicsPipelineBuilder(const Device& device);

        GraphicsPipelineBuilder& SetShaders(const ShaderModule& vertShader, const ShaderModule& fragShader);
        GraphicsPipelineBuilder& SetVertexInput(const VkVertexInputBindingDescription& binding,
                                                const VkVertexInputAttributeDescription* attributes,
                                                uint32_t attributeCount);
        GraphicsPipelineBuilder& SetFixedViewport(VkOffset2D offset, VkExtent2D extent);
        GraphicsPipelineBuilder& SetDynamicViewportAndScissor();
        GraphicsPipelineBuilder& SetPolygonMode(VkPolygonMode mode);
        GraphicsPipelineBuilder& SetDepthBias(float constantFactor, float slopeFactor);
        GraphicsPipelineBuilder& SetDepth(bool testEnable, bool writeEnable, VkCompareOp compareOp);
        GraphicsPipelineBuilder& SetAlphaBlend(VkBlendFactor srcAlphaFactor, VkBlendFactor dstAlphaFactor);
        GraphicsPipelineBuilder& SetColorAttachmentCount(uint32_t count); // 0 = depth-only pass

        VkPipeline Build(VkPipelineLayout pipelineLayout, VkRenderPass renderPass, const char* errorLabel) const;

    private:
        const Device& device_;

        std::vector<VkPipelineShaderStageCreateInfo> shaderStages_;
        VkVertexInputBindingDescription vertexBinding_{};
        std::vector<VkVertexInputAttributeDescription> vertexAttributes_;
        bool hasVertexInput_ = false;
        VkViewport viewport_{};
        VkRect2D scissor_{};
        bool dynamicViewport_ = false;
        VkPolygonMode polygonMode_ = VK_POLYGON_MODE_FILL;
        bool depthBiasEnable_ = false;
        float depthBiasConstantFactor_ = 0.0f;
        float depthBiasSlopeFactor_ = 0.0f;
        bool depthTestEnable_ = false;
        bool depthWriteEnable_ = false;
        VkCompareOp depthCompareOp_ = VK_COMPARE_OP_LESS;
        bool blendEnable_ = false;
        VkBlendFactor srcAlphaBlendFactor_ = VK_BLEND_FACTOR_ONE;
        VkBlendFactor dstAlphaBlendFactor_ = VK_BLEND_FACTOR_ZERO;
        uint32_t colorAttachmentCount_ = 1;
    };
}
