#include "ShadowMapPass.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Vertex.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"

namespace Vulkan::Shadow
{
    namespace
    {
        constexpr VkFormat kShadowFormat = VK_FORMAT_D32_SFLOAT;
    }

    ShadowMapPass::ShadowMapPass(const Vulkan::Device& device) : device_(device)
    {
    }

    ShadowMapPass::~ShadowMapPass()
    {
        DestroyResources();
    }

    void ShadowMapPass::CreateResources(const Assets::Scene& scene)
    {
        DestroyResources();

        // Render pass：depth-only。clear -> store -> end layout = depth_read_only
        {
            VkAttachmentDescription depthAttachment{};
            depthAttachment.format = kShadowFormat;
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

            VkAttachmentReference depthRef{};
            depthRef.attachment = 0;
            depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 0;
            subpass.pDepthStencilAttachment = &depthRef;

            std::array<VkSubpassDependency, 2> deps{};
            deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
            deps[0].dstSubpass = 0;
            deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            deps[1].srcSubpass = 0;
            deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
            deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            deps[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

            VkRenderPassCreateInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpInfo.attachmentCount = 1;
            rpInfo.pAttachments = &depthAttachment;
            rpInfo.subpassCount = 1;
            rpInfo.pSubpasses = &subpass;
            rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
            rpInfo.pDependencies = deps.data();

            Check(vkCreateRenderPass(device_.Handle(), &rpInfo, nullptr, &renderPass_), "create shadow render pass");
        }

        // Pipeline layout：复用 bindless 全局描述符集 + GPUScene push constant
        {
            std::vector<DescriptorSetManager*> managers = {
                &Assets::GlobalTexturePool::GetInstance()->GetDescriptorManager(),
            };
            VkPushConstantRange pushConstantRange{};
            pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
            pushConstantRange.offset = 0;
            pushConstantRange.size = sizeof(Assets::GPUScene);
            pipelineLayout_.reset(new PipelineLayout(device_, managers, 1, &pushConstantRange, 1));
        }

        // 图形管线
        {
            const VkExtent2D extent{Assets::Scene::kSunShadowResolution, Assets::Scene::kSunShadowResolution};

            VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
            vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

            VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
            inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(extent.width);
            viewport.height = static_cast<float>(extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = extent;

            VkPipelineViewportStateCreateInfo viewportState{};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.pViewports = &viewport;
            viewportState.scissorCount = 1;
            viewportState.pScissors = &scissor;

            VkPipelineRasterizationStateCreateInfo rasterizer{};
            rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizer.lineWidth = 1.0f;
            rasterizer.cullMode = VK_CULL_MODE_NONE;
            rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizer.depthBiasEnable = VK_TRUE;
            rasterizer.depthBiasConstantFactor = 1.25f;
            rasterizer.depthBiasSlopeFactor = 1.75f;

            VkPipelineMultisampleStateCreateInfo multisampling{};
            multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            multisampling.minSampleShading = 1.0f;

            VkPipelineDepthStencilStateCreateInfo depthStencil{};
            depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
            depthStencil.minDepthBounds = 0.0f;
            depthStencil.maxDepthBounds = 1.0f;

            VkPipelineColorBlendStateCreateInfo colorBlending{};
            colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlending.attachmentCount = 0;
            colorBlending.pAttachments = nullptr;

            const ShaderModule fragShader(device_, "assets/shaders/Rast.ShadowMap.frag.slang.spv");
            auto createPipeline = [&](const char* vertexShaderPath, VkPipeline& outPipeline, const char* debugName)
            {
                const ShaderModule vertShader(device_, vertexShaderPath);
                VkPipelineShaderStageCreateInfo stages[] = {
                    vertShader.CreateShaderStage(VK_SHADER_STAGE_VERTEX_BIT),
                    fragShader.CreateShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT),
                };

                VkGraphicsPipelineCreateInfo pipelineInfo{};
                pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipelineInfo.stageCount = 2;
                pipelineInfo.pStages = stages;
                pipelineInfo.pVertexInputState = &vertexInputInfo;
                pipelineInfo.pInputAssemblyState = &inputAssembly;
                pipelineInfo.pViewportState = &viewportState;
                pipelineInfo.pRasterizationState = &rasterizer;
                pipelineInfo.pMultisampleState = &multisampling;
                pipelineInfo.pDepthStencilState = &depthStencil;
                pipelineInfo.pColorBlendState = &colorBlending;
                pipelineInfo.layout = pipelineLayout_->Handle();
                pipelineInfo.renderPass = renderPass_;
                pipelineInfo.subpass = 0;

                Check(vkCreateGraphicsPipelines(device_.Handle(), nullptr, 1, &pipelineInfo, nullptr, &outPipeline),
                    debugName);
            };

            createPipeline("assets/shaders/Rast.ShadowMap.vert.slang.spv", pipeline_,
                           "create shadow graphics pipeline");
            createPipeline("assets/shaders/Rast.ShadowMapSoftMeshShader.vert.slang.spv", softMeshShaderPipeline_,
                           "create shadow SoftMeshShader graphics pipeline");
        }

        // 4 个 framebuffer
        for (uint32_t i = 0; i < Assets::Scene::kSunShadowCascadeCount; ++i)
        {
            VkImageView attachment = scene.SunShadowImageView(i).Handle();
            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = renderPass_;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = &attachment;
            fbInfo.width = Assets::Scene::kSunShadowResolution;
            fbInfo.height = Assets::Scene::kSunShadowResolution;
            fbInfo.layers = 1;
            Check(vkCreateFramebuffer(device_.Handle(), &fbInfo, nullptr, &frameBuffers_[i]),
                "create shadow framebuffer");
        }
    }

    void ShadowMapPass::DestroyResources()
    {
        for (auto& fb : frameBuffers_)
        {
            if (fb)
            {
                vkDestroyFramebuffer(device_.Handle(), fb, nullptr);
                fb = VK_NULL_HANDLE;
            }
        }

        if (pipeline_)
        {
            vkDestroyPipeline(device_.Handle(), pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (softMeshShaderPipeline_)
        {
            vkDestroyPipeline(device_.Handle(), softMeshShaderPipeline_, nullptr);
            softMeshShaderPipeline_ = VK_NULL_HANDLE;
        }

        pipelineLayout_.reset();

        if (renderPass_)
        {
            vkDestroyRenderPass(device_.Handle(), renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
    }

    void ShadowMapPass::DrawCascade(
        VkCommandBuffer commandBuffer, const Assets::Scene& scene, const Assets::GPUScene& gpuSceneBase,
        uint32_t cascade, VkDeviceSize indirectDrawOffset, bool softMeshShader)
    {
        VkPipeline activePipeline = softMeshShader ? softMeshShaderPipeline_ : pipeline_;
        if (!activePipeline)
        {
            return;
        }

        if (!softMeshShader)
        {
            const VkBuffer indexBuffer = scene.IndexBuffer().Handle();
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
        }

        VkClearValue clearValue{};
        clearValue.depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass_;
        rpBegin.framebuffer = frameBuffers_[cascade];
        rpBegin.renderArea.offset = {0, 0};
        rpBegin.renderArea.extent = {Assets::Scene::kSunShadowResolution, Assets::Scene::kSunShadowResolution};
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearValue;

        vkCmdBeginRenderPass(commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);
        pipelineLayout_->BindDescriptorSets(commandBuffer, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);

        Assets::GPUScene gpuScene = gpuSceneBase;
        gpuScene.custom_data_0 = cascade;
        gpuScene.custom_data_2 = cascade * scene.GetMaxSceneTriangles();
        vkCmdPushConstants(commandBuffer, pipelineLayout_->Handle(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(Assets::GPUScene), &gpuScene);

        if (softMeshShader)
        {
            const uint32_t slot = scene.SoftMeshShaderDrawSlotForShadowCascade(cascade);
            vkCmdDrawIndirect(commandBuffer, scene.SoftMeshShaderDrawArgBuffer().Handle(), scene.SoftMeshShaderDrawArgByteOffset(slot),
                              1, sizeof(VkDrawIndirectCommand));
        }
        else
        {
            vkCmdDrawIndexedIndirect(commandBuffer, scene.ShadowIndirectDrawBuffer().Handle(), indirectDrawOffset,
                                     scene.GetIndirectDrawBatchCount(),
                                     sizeof(VkDrawIndexedIndirectCommand));
        }
        vkCmdEndRenderPass(commandBuffer);
    }
}
