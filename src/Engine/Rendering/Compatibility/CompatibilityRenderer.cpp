#include "Engine/Rendering/Compatibility/CompatibilityRenderer.hpp"

#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"
#include "Engine/Vulkan/DescriptorSystem.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/GraphicsPipelineBuilder.hpp"
#include "Engine/Vulkan/MemoryAndShader.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"

namespace Vulkan::Compatibility
{
    namespace
    {
        // Dark neutral, so unlit base-colour geometry reads clearly against it and an empty scene
        // still looks deliberate rather than broken.
        constexpr VkClearColorValue kBackgroundColor{{0.05f, 0.05f, 0.06f, 1.0f}};
    }

    CompatibilityRenderer::~CompatibilityRenderer()
    {
        CompatibilityRenderer::DeleteSwapChain();
    }

    void CompatibilityRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        (void)extent;
        const auto& device = Device();
        const class SwapChain& swapChain = SwapChain();

        // Storage buffers rather than a uniform buffer: std430 array stride matches the CPU-side
        // struct layout, while std140 would pad NodeProxy::matId[16] from 4 to 16 bytes per element.
        const std::vector<DescriptorBinding> descriptorBindings = {
            {EB_Nodes, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
            {EB_VertexWords, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
            {EB_Indices, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
            {EB_Offsets, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
            {EB_Materials, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT},
        };
        descriptorSetManager_.reset(new DescriptorSetManager(device, descriptorBindings, 1));
        boundBuffers_.fill(VK_NULL_HANDLE);

        // Deliberately not the bindless set: this profile's GlobalTexturePool layout is a stub the
        // normal pipelines could not use anyway, and binding it here would tie this pass to a
        // descriptor contract the device cannot back.
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(FPushConstants);
        std::vector<DescriptorSetManager*> managers = {descriptorSetManager_.get()};
        pipelineLayout_.reset(new class PipelineLayout(device, managers, 1, &pushConstantRange, 1));

        // Renders straight into the swapchain image; CLEAR replaces a separate clear pass. The
        // caller leaves the image in COLOR_ATTACHMENT_OPTIMAL and the base renderer moves it to
        // PRESENT_SRC afterwards, so both layouts are stated explicitly rather than defaulted.
        renderPass_.reset(new class RenderPass(
            swapChain, swapChain.Format(), baseRender_.DepthBuffer(),
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL));
        renderPass_->SetDebugName("Compatibility Render Pass");

        const ShaderModule vertShader(device, "assets/shaders/Rast.CompatibilityAlbedo.vert.slang.spv");
        const ShaderModule fragShader(device, "assets/shaders/Rast.CompatibilityAlbedo.frag.slang.spv");

        pipeline_ = GraphicsPipelineBuilder(device)
            .SetShaders(vertShader, fragShader)
            .SetDynamicViewportAndScissor()
            .SetDepth(true, true, VK_COMPARE_OP_LESS)
            .Build(pipelineLayout_->Handle(), renderPass_->Handle(), "create compatibility graphics pipeline");

        frameBuffers_.clear();
        frameBuffers_.reserve(swapChain.ImageViews().size());
        for (const auto& imageView : swapChain.ImageViews())
        {
            frameBuffers_.emplace_back(swapChain.Extent(), *imageView, *renderPass_);
        }
    }

    void CompatibilityRenderer::DeleteSwapChain()
    {
        frameBuffers_.clear();
        if (pipeline_ != VK_NULL_HANDLE)
        {
            vkDestroyPipeline(Device().Handle(), pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        renderPass_.reset();
        pipelineLayout_.reset();
        descriptorSetManager_.reset();
        boundBuffers_.fill(VK_NULL_HANDLE);
    }

    void CompatibilityRenderer::BindSceneBuffers(const Assets::Scene& scene)
    {
        // Nodes and Materials are two windows onto the same scene-dynamic buffer; the offsets are
        // the ones the shaders would otherwise reach by pointer arithmetic off SceneDynamicBase.
        const VkBuffer sceneDynamic = scene.NodeMatrixBuffer().Handle();
        const std::array<VkDescriptorBufferInfo, EB_Count> bufferInfos{
            VkDescriptorBufferInfo{sceneDynamic, Assets::GPU_SCENE_DYNAMIC_NODES_OFFSET,
                                   Assets::GPU_SCENE_NODE_PROXY_SIZE * Assets::MAX_RENDER_PROXIES},
            VkDescriptorBufferInfo{scene.VertexBuffer().Handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{scene.PrimAddressBuffer().Handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{scene.OffsetBuffer().Handle(), 0, VK_WHOLE_SIZE},
            VkDescriptorBufferInfo{sceneDynamic, Assets::GPU_SCENE_DYNAMIC_MATERIALS_OFFSET,
                                   Assets::GPU_SCENE_MATERIAL_SIZE * Assets::MAX_MATERIALS},
        };

        bool changed = false;
        for (uint32_t binding = 0; binding < EB_Count; ++binding)
        {
            changed = changed || boundBuffers_[binding] != bufferInfos[binding].buffer;
        }
        if (!changed)
        {
            return;
        }

        auto& descriptorSets = descriptorSetManager_->DescriptorSets();
        std::vector<VkWriteDescriptorSet> writes;
        writes.reserve(EB_Count);
        for (uint32_t binding = 0; binding < EB_Count; ++binding)
        {
            writes.push_back(descriptorSets.Bind(0, binding, bufferInfos[binding]));
            boundBuffers_[binding] = bufferInfos[binding].buffer;
        }
        descriptorSets.UpdateDescriptors(0, writes);
    }

    void CompatibilityRenderer::Render(const VkCommandBuffer commandBuffer, const uint32_t imageIndex)
    {
        if (pipeline_ == VK_NULL_HANDLE || imageIndex >= frameBuffers_.size())
        {
            return;
        }

        SCOPED_GPU_TIMER("compatibility albedo");

        const Assets::Scene& scene = GetScene();
        BindSceneBuffers(scene);

        // The pass declares COLOR_ATTACHMENT_OPTIMAL as its initial layout and clears, so the
        // previous contents are irrelevant -- but the tracker still has to be told, or the base
        // renderer's transition to PRESENT_SRC afterwards would barrier from the wrong layout.
        baseRender_.TransitionSwapchainImage(
            commandBuffer, imageIndex,
            {.stages = PipelineCommon::ERenderStage::ColorAttachment,
             .access = PipelineCommon::EResourceAccess::ColorWrite,
             .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
             .discardPreviousContents = true},
            "compatibility albedo");

        const class SwapChain& swapChain = SwapChain();
        const std::array<VkClearValue, 2> clearValues{
            VkClearValue{.color = kBackgroundColor},
            VkClearValue{.depthStencil = {1.0f, 0}},
        };

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = renderPass_->Handle();
        renderPassInfo.framebuffer = frameBuffers_[imageIndex].Handle();
        renderPassInfo.renderArea.offset = {0, 0};
        // Whole image, so the clear also covers whatever an editor host reserved for its panels.
        renderPassInfo.renderArea.extent = swapChain.Extent();
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            // The scene itself goes into the presented sub-rect, which is the whole window unless a
            // host reserved part of it.
            const VkViewport viewport{
                static_cast<float>(swapChain.OutputOffset().x),
                static_cast<float>(swapChain.OutputOffset().y),
                static_cast<float>(swapChain.OutputExtent().width),
                static_cast<float>(swapChain.OutputExtent().height),
                0.0f, 1.0f};
            const VkRect2D scissor{swapChain.OutputOffset(), swapChain.OutputExtent()};
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
            pipelineLayout_->BindDescriptorSets(commandBuffer, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);

            const Assets::UniformBufferObject& camera = baseRender_.LastUniformBufferObject();
            FPushConstants pushConstants{};
            pushConstants.ViewProjection = camera.ViewProjection;
            pushConstants.SunDirection = camera.SunDirection;
            // Colours forwarded as-is; w says whether the scene has that light. The shader takes
            // only the hue -- SunIntensity / SkyIntensity are scaled against the sky IBL it cannot
            // sample, so passing their magnitudes would just blow the preview out.
            pushConstants.SunColor = glm::vec4(glm::vec3(camera.SunColor), camera.HasSun ? 1.0f : 0.0f);
            pushConstants.SkyColor = glm::vec4(glm::vec3(camera.SkyColor), camera.HasSky ? 1.0f : 0.0f);

            const std::vector<Assets::NodeProxy>& proxies = scene.GetNodeProxies();
            // Indexed by the *encoded* model-section id that NodeProxy::modelId already carries,
            // matching how the shader indexes Offsets. Do not decode it here.
            const std::vector<Assets::ModelData>& models = scene.Offsets();
            for (uint32_t proxyIndex = 0; proxyIndex < proxies.size(); ++proxyIndex)
            {
                const Assets::NodeProxy& proxy = proxies[proxyIndex];
                if ((proxy.visible & Runtime::RenderParticipation::mainVisibility) == 0 ||
                    proxy.modelId >= models.size() || models[proxy.modelId].indexCount == 0)
                {
                    continue;
                }

                pushConstants.ProxyIndex = proxyIndex;
                vkCmdPushConstants(commandBuffer, pipelineLayout_->Handle(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(pushConstants), &pushConstants);
                vkCmdDraw(commandBuffer, models[proxy.modelId].indexCount, 1, 0, 0);
            }
        }
        vkCmdEndRenderPass(commandBuffer);
    }
}
