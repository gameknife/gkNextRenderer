#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"

namespace Vulkan
{
    GraphicsPipelineBuilder::GraphicsPipelineBuilder(const Device& device) : device_(device)
    {
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetShaders(const ShaderModule& vertShader, const ShaderModule& fragShader)
    {
        shaderStages_ = {
            vertShader.CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT),
            fragShader.CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT),
        };
        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetVertexInput(const VkVertexInputBindingDescription& binding,
                                                                     const VkVertexInputAttributeDescription* attributes,
                                                                     uint32_t attributeCount)
    {
        vertexBinding_ = binding;
        vertexAttributes_.assign(attributes, attributes + attributeCount);
        hasVertexInput_ = true;
        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetFixedViewport(VkOffset2D offset, VkExtent2D extent)
    {
        viewport_ = {static_cast<float>(offset.x), static_cast<float>(offset.y),
                     static_cast<float>(extent.width), static_cast<float>(extent.height), 0.0f, 1.0f};
        scissor_ = {offset, extent};
        dynamicViewport_ = false;
        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetDynamicViewportAndScissor()
    {
        dynamicViewport_ = true;
        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetPolygonMode(VkPolygonMode mode)
    {
        polygonMode_ = mode;
        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetDepthBias(float constantFactor, float slopeFactor)
    {
        depthBiasEnable_ = true;
        depthBiasConstantFactor_ = constantFactor;
        depthBiasSlopeFactor_ = slopeFactor;
        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetDepth(bool testEnable, bool writeEnable, VkCompareOp compareOp)
    {
        depthTestEnable_ = testEnable;
        depthWriteEnable_ = writeEnable;
        depthCompareOp_ = compareOp;
        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetAlphaBlend(VkBlendFactor srcAlphaFactor, VkBlendFactor dstAlphaFactor)
    {
        blendEnable_ = true;
        srcAlphaBlendFactor_ = srcAlphaFactor;
        dstAlphaBlendFactor_ = dstAlphaFactor;
        return *this;
    }

    GraphicsPipelineBuilder& GraphicsPipelineBuilder::SetColorAttachmentCount(uint32_t count)
    {
        colorAttachmentCount_ = count;
        return *this;
    }

    VkPipeline GraphicsPipelineBuilder::Build(VkPipelineLayout pipelineLayout, VkRenderPass renderPass, const char* errorLabel) const
    {
        VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        if (hasVertexInput_)
        {
            vertexInputInfo.vertexBindingDescriptionCount = 1;
            vertexInputInfo.pVertexBindingDescriptions = &vertexBinding_;
            vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes_.size());
            vertexInputInfo.pVertexAttributeDescriptions = vertexAttributes_.data();
        }

        VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState = {};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;
        if (!dynamicViewport_)
        {
            viewportState.pViewports = &viewport_;
            viewportState.pScissors = &scissor_;
        }

        VkPipelineRasterizationStateCreateInfo rasterizer = {};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = polygonMode_;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = depthBiasEnable_ ? VK_TRUE : VK_FALSE;
        rasterizer.depthBiasConstantFactor = depthBiasConstantFactor_;
        rasterizer.depthBiasSlopeFactor = depthBiasSlopeFactor_;

        VkPipelineMultisampleStateCreateInfo multisampling = {};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f;

        VkPipelineDepthStencilStateCreateInfo depthStencil = {};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = depthTestEnable_ ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = depthWriteEnable_ ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = depthCompareOp_;
        depthStencil.depthBoundsTestEnable = VK_FALSE;
        depthStencil.minDepthBounds = 0.0f;
        depthStencil.maxDepthBounds = 1.0f;
        depthStencil.stencilTestEnable = VK_FALSE;

        VkPipelineColorBlendAttachmentState colorBlendAttachment = {};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                              VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachment.blendEnable = blendEnable_ ? VK_TRUE : VK_FALSE;
        colorBlendAttachment.srcColorBlendFactor = blendEnable_ ? VK_BLEND_FACTOR_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
        colorBlendAttachment.dstColorBlendFactor = blendEnable_ ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ZERO;
        colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachment.srcAlphaBlendFactor = srcAlphaBlendFactor_;
        colorBlendAttachment.dstAlphaBlendFactor = dstAlphaBlendFactor_;
        colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

        std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(
            colorAttachmentCount_, colorBlendAttachment);

        VkPipelineColorBlendStateCreateInfo colorBlending = {};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.logicOp = VK_LOGIC_OP_COPY;
        colorBlending.attachmentCount = colorAttachmentCount_;
        colorBlending.pAttachments = colorBlendAttachments.empty() ? nullptr : colorBlendAttachments.data();

        const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState = {};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = 2;
        dynamicState.pDynamicStates = dynamicStates;

        VkGraphicsPipelineCreateInfo pipelineInfo = {};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages_.size());
        pipelineInfo.pStages = shaderStages_.data();
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = dynamicViewport_ ? &dynamicState : nullptr;
        pipelineInfo.basePipelineHandle = nullptr;
        pipelineInfo.basePipelineIndex = -1;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        Check(vkCreateGraphicsPipelines(device_.Handle(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline),
              errorLabel);
        return pipeline;
    }
}
