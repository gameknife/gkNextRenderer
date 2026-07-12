#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/Data/Vertex.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"

#include <filesystem>

namespace Vulkan::Shadow
{
    namespace
    {
        constexpr VkFormat kShadowFormat = VK_FORMAT_D32_SFLOAT;

        std::string ShaderFilename(const std::string& shaderFile)
        {
            return std::filesystem::path(shaderFile).filename().string();
        }

        bool MarkChangedShaderFile(
            const std::string& shaderFile,
            const std::set<std::string>& changedShaderFiles,
            std::set<std::string>& handledShaderFiles)
        {
            const std::string filename = ShaderFilename(shaderFile);
            if (changedShaderFiles.find(filename) == changedShaderFiles.end())
            {
                return false;
            }
            handledShaderFiles.insert(filename);
            return true;
        }
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

            const ShaderModule fragShader(device_, "assets/shaders/Rast.ShadowMap.frag.slang.spv");
            const ShaderModule vertShader(device_, "assets/shaders/Rast.ShadowMapSoftMeshShader.vert.slang.spv");

            pipeline_ = GraphicsPipelineBuilder(device_)
                .SetShaders(vertShader, fragShader)
                .SetFixedViewport({0, 0}, extent)
                .SetDepth(true, true, VK_COMPARE_OP_LESS)
                .SetDepthBias(1.25f, 1.75f)
                .SetColorAttachmentCount(0)
                .Build(pipelineLayout_->Handle(), renderPass_, "create shadow graphics pipeline");
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

    void ShadowMapPass::RecreatePipeline()
    {
        if (pipeline_)
        {
            vkDestroyPipeline(device_.Handle(), pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }

        const VkExtent2D extent{Assets::Scene::kSunShadowResolution, Assets::Scene::kSunShadowResolution};
        const ShaderModule fragShader(device_, "assets/shaders/Rast.ShadowMap.frag.slang.spv");
        const ShaderModule vertShader(device_, "assets/shaders/Rast.ShadowMapSoftMeshShader.vert.slang.spv");

        pipeline_ = GraphicsPipelineBuilder(device_)
            .SetShaders(vertShader, fragShader)
            .SetFixedViewport({0, 0}, extent)
            .SetDepth(true, true, VK_COMPARE_OP_LESS)
            .SetDepthBias(1.25f, 1.75f)
            .SetColorAttachmentCount(0)
            .Build(pipelineLayout_->Handle(), renderPass_, "recreate shadow graphics pipeline");
    }

    void ShadowMapPass::ReloadShaders(
        const std::set<std::string>& changedShaderFiles,
        std::set<std::string>& handledShaderFiles)
    {
        constexpr const char* vertexShader = "assets/shaders/Rast.ShadowMapSoftMeshShader.vert.slang.spv";
        constexpr const char* fragmentShader = "assets/shaders/Rast.ShadowMap.frag.slang.spv";
        const bool reloadVertex = changedShaderFiles.find(ShaderFilename(vertexShader)) != changedShaderFiles.end();
        const bool reloadFragment = changedShaderFiles.find(ShaderFilename(fragmentShader)) != changedShaderFiles.end();
        if (!reloadVertex && !reloadFragment)
        {
            return;
        }

        RecreatePipeline();
        MarkChangedShaderFile(vertexShader, changedShaderFiles, handledShaderFiles);
        MarkChangedShaderFile(fragmentShader, changedShaderFiles, handledShaderFiles);
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
        pipelineLayout_.reset();

        if (renderPass_)
        {
            vkDestroyRenderPass(device_.Handle(), renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
    }

    void ShadowMapPass::DrawCascade(
        VkCommandBuffer commandBuffer, const Assets::Scene& scene, const Assets::GPUScene& gpuSceneBase,
        uint32_t cascade)
    {
        if (!pipeline_)
        {
            return;
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
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        pipelineLayout_->BindDescriptorSets(commandBuffer, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);

        Assets::GPUScene gpuScene = gpuSceneBase;
        gpuScene.CustomData0 = cascade;
        gpuScene.CustomData2 = cascade * scene.GetMaxSceneTriangles();
        vkCmdPushConstants(commandBuffer, pipelineLayout_->Handle(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(Assets::GPUScene), &gpuScene);

        const uint32_t slot = scene.SoftMeshShaderDrawSlotForShadowCascade(cascade);
        vkCmdDrawIndirect(commandBuffer, scene.SoftMeshShaderDrawArgBuffer().Handle(),
                          scene.SoftMeshShaderDrawArgByteOffset(slot), 1, sizeof(VkDrawIndirectCommand));
        vkCmdEndRenderPass(commandBuffer);
    }
}
