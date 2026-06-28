// VulkanBaseRenderer GPU-driven frame dispatch: skinning, culling,
// visibility, sun shadow, wireframe overlay and visual debugger passes.
// Split from VulkanBaseRenderer.cpp; same class, separate TU.
#include "Engine/Rendering/VulkanBaseRenderer.hpp"
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Node.h"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include "Engine/Runtime/Components/RenderComponent.h"
#include "Engine/Runtime/Components/SkinnedMeshComponent.h"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/BufferUtil.hpp"
#include "Engine/Vulkan/CommandExecution.hpp"
#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/Device.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/RayTracing/DeviceProcedures.hpp"
#include "Engine/Vulkan/RayTracing/RayTracingProperties.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"
#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/SyncAndTiming.hpp"

namespace Vulkan
{
    void VulkanBaseRenderer::UpdateSkinningBuffers()
    {
        auto& scene = GetScene();
        uint32_t vertCount = scene.GetVerticeCount();
        if (vertCount == 0) return;

        int flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        int rtxFlags = caps_.supportRayTracing ? VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR : 0;

        size_t requiredVertexSize = vertCount * sizeof(Assets::GPUVertex);
        if (!skin_.vertexBuffer || skin_.vertexBufferSize < requiredVertexSize)
        {
            Vulkan::BufferUtil::CreateDeviceBufferLocal(*ctx_.commandPool, "SkinnedVertices", flags | rtxFlags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, requiredVertexSize, skin_.vertexBuffer, skin_.vertexMemory);
            skin_.vertexBufferSize = (uint32_t)requiredVertexSize;
        }

        uint32_t totalJoints = 0;
        for (auto& node : scene.Nodes())
        {
            if (auto* skinnedMesh = node->GetComponentPtr<Runtime::SkinnedMeshComponent>())
            {
                totalJoints += (uint32_t)skinnedMesh->GetJointMatrices().size();
            }
        }

        if (totalJoints > 0)
        {
            size_t requiredJointSize = totalJoints * sizeof(glm::mat4);
            if (!skin_.jointBuffer || skin_.jointBufferSize < requiredJointSize)
            {
                Vulkan::BufferUtil::CreateDeviceBufferLocal(*ctx_.commandPool, "JointMatrices", flags, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, requiredJointSize, skin_.jointBuffer, skin_.jointMemory);
                skin_.jointBufferSize = (uint32_t)requiredJointSize;
            }

            // Map and upload
            glm::mat4* data = (glm::mat4*)skin_.jointMemory->Map(0, requiredJointSize);
            uint32_t offset = 0;
            for (auto& node : scene.Nodes())
            {
                if (auto* skinnedMesh = node->GetComponentPtr<Runtime::SkinnedMeshComponent>())
                {
                    const auto& matrices = skinnedMesh->GetJointMatrices();
                    std::memcpy(data + offset, matrices.data(), matrices.size() * sizeof(glm::mat4));
                    offset += (uint32_t)matrices.size();
                }
            }
            skin_.jointMemory->Unmap();
        }
    }

    void VulkanBaseRenderer::DispatchSkinning(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("skinning pass");
        auto& scene = GetScene();

        if (skin_.vertexBuffer)
        {
            scene.SetSkinningBuffers(skin_.vertexBuffer->GetDeviceAddress(),
                                     skin_.jointBuffer ? skin_.jointBuffer->GetDeviceAddress() : 0);
        }
        else
        {
            scene.SetSkinningBuffers(0, 0);
        }

        skin_.pipeline->BindPipeline(commandBuffer, scene, imageIndex);

        Assets::GPUScene gpuScene = scene.FetchGPUScene(imageIndex);
        if (!skin_.vertexBuffer)
        {
            return;
        }

        for (size_t i = 0; i < skin_.updateRequests.size(); i++)
        {
            uint32_t modelId = skin_.updateRequests[i];
            if (modelId == static_cast<uint32_t>(-1))
            {
                continue;
            }
            const auto* model = scene.GetModel(modelId);
            if (!model)
            {
                continue;
            }

            uint32_t proxyIdx = std::numeric_limits<uint32_t>::max();
            const uint32_t skinnedProxyModelId = modelId * 10;
            const auto& nodeProxys = scene.GetNodeProxys();
            for (uint32_t nodeIdx = 0; nodeIdx < nodeProxys.size(); ++nodeIdx)
            {
                if (nodeProxys[nodeIdx].modelId == skinnedProxyModelId)
                {
                    proxyIdx = nodeIdx;
                    break;
                }
            }
            if (proxyIdx == std::numeric_limits<uint32_t>::max())
            {
                continue;
            }

            uint32_t vertexOffset = scene.Offsets()[modelId * 10].vertexOffset;
            uint32_t vertexCount = model->NumberOfVertices();

            gpuScene.custom_data_0 = proxyIdx;
            gpuScene.custom_data_1 = vertexOffset;
            gpuScene.custom_data_2 = vertexCount;

            vkCmdPushConstants(commandBuffer, skin_.pipeline->PipelineLayout().Handle(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
            uint32_t groupCount = (vertexCount + 63) / 64;
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);
        }

        // Skinned vertex buffer barrier: compute write -> shader/RT read.
        VkBufferMemoryBarrier skinnedBufferBarrier = {};
        skinnedBufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        skinnedBufferBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        skinnedBufferBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkPipelineStageFlags skinnedDstStages =
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        if (caps_.supportRayTracing)
        {
            skinnedBufferBarrier.dstAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            skinnedDstStages |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        }
        skinnedBufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        skinnedBufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        skinnedBufferBarrier.buffer = skin_.vertexBuffer->Handle();
        skinnedBufferBarrier.offset = 0;
        skinnedBufferBarrier.size = VK_WHOLE_SIZE;

        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             skinnedDstStages, 0, 0, nullptr, 1, &skinnedBufferBarrier, 0, nullptr);
    }

    void VulkanBaseRenderer::DispatchGpuCulling(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("gpu cull");

        const uint32_t indirectDrawBatchCount = GetScene().GetIndirectDrawBatchCount();
        if (indirectDrawBatchCount == 0)
        {
            return;
        }

        VkBufferMemoryBarrier nodeMatrixBarrier = {};
        nodeMatrixBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        nodeMatrixBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        nodeMatrixBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        nodeMatrixBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        nodeMatrixBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        nodeMatrixBarrier.buffer = GetScene().NodeMatrixBuffer().Handle();
        nodeMatrixBarrier.offset = 0;
        nodeMatrixBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, 1, &nodeMatrixBarrier, 0, nullptr);

        auto& scene = GetScene();
        vkCmdFillBuffer(commandBuffer, scene.SoftMeshShaderCounterBuffer().Handle(), 0, VK_WHOLE_SIZE, 0);

        VkBufferMemoryBarrier counterClearBarrier = {};
        counterClearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        counterClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        counterClearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        counterClearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        counterClearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        counterClearBarrier.buffer = scene.SoftMeshShaderCounterBuffer().Handle();
        counterClearBarrier.offset = 0;
        counterClearBarrier.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                             1, &counterClearBarrier, 0, nullptr);

        auto bindCompute = [&](const PipelineCommon::ZeroBindPipeline& pipeline)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Handle());
            pipeline.PipelineLayout().BindDescriptorSets(commandBuffer, 0);
        };

        Assets::GPUScene gpuScene = scene.FetchGPUScene(imageIndex);
        gpuScene.custom_data_1 = indirectDrawBatchCount;
        gpuScene.custom_data_2 = scene.GetMaxSceneTriangles();

        bindCompute(*overlay_.gpuCullCompactPipeline);
        vkCmdPushConstants(commandBuffer, overlay_.gpuCullCompactPipeline->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        uint32_t groupCount = (indirectDrawBatchCount + 63) / 64;
        vkCmdDispatch(commandBuffer, groupCount, 1, 1);

        std::array<VkBufferMemoryBarrier, 2> compactBarriers{};
        compactBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        compactBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        compactBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        compactBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        compactBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        compactBarriers[0].buffer = scene.SoftMeshShaderCounterBuffer().Handle();
        compactBarriers[0].offset = 0;
        compactBarriers[0].size = VK_WHOLE_SIZE;
        compactBarriers[1] = compactBarriers[0];
        compactBarriers[1].buffer = scene.SoftMeshShaderVisibleItemBuffer().Handle();
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                             static_cast<uint32_t>(compactBarriers.size()), compactBarriers.data(),
                             0, nullptr);

        bindCompute(*overlay_.softMeshShaderFinalizePipeline);
        gpuScene.custom_data_0 = 0;
        vkCmdPushConstants(commandBuffer, overlay_.softMeshShaderFinalizePipeline->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatch(commandBuffer, 1, 1, 1);

        std::array<VkBufferMemoryBarrier, 2> expandInputBarriers{};
        expandInputBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        expandInputBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        expandInputBarriers[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        expandInputBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expandInputBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expandInputBarriers[0].buffer = scene.SoftMeshShaderDispatchArgBuffer().Handle();
        expandInputBarriers[0].offset = 0;
        expandInputBarriers[0].size = sizeof(VkDispatchIndirectCommand);
        expandInputBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        expandInputBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        expandInputBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        expandInputBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expandInputBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        expandInputBarriers[1].buffer = scene.SoftMeshShaderVisibleItemBuffer().Handle();
        expandInputBarriers[1].offset = 0;
        expandInputBarriers[1].size = sizeof(Assets::SoftMeshShaderVisibleItem) * Assets::Scene::kMaxIndirectDrawCount;
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 0, nullptr, static_cast<uint32_t>(expandInputBarriers.size()),
                             expandInputBarriers.data(), 0, nullptr);

        bindCompute(*overlay_.softMeshShaderExpandPipeline);
        vkCmdPushConstants(commandBuffer, overlay_.softMeshShaderExpandPipeline->PipelineLayout().Handle(),
                           VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
        vkCmdDispatchIndirect(commandBuffer, scene.SoftMeshShaderDispatchArgBuffer().Handle(), 0);

        std::array<VkBufferMemoryBarrier, 2> drawBarriers{};
        drawBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        drawBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        drawBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        drawBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        drawBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        drawBarriers[0].buffer = scene.SoftMeshShaderPrimBuffer().Handle();
        drawBarriers[0].offset = 0;
        drawBarriers[0].size = VK_WHOLE_SIZE;
        drawBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        drawBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        drawBarriers[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        drawBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        drawBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        drawBarriers[1].buffer = scene.SoftMeshShaderDrawArgBuffer().Handle();
        drawBarriers[1].offset = 0;
        drawBarriers[1].size = sizeof(VkDrawIndirectCommand);
        vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                             0, 0, nullptr, static_cast<uint32_t>(drawBarriers.size()), drawBarriers.data(),
                             0, nullptr);
    }

    void VulkanBaseRenderer::DispatchClearPass(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool clearSwapchain)
    {
        SCOPED_GPU_TIMER("clear pass");

        // Clears the active view's screen-space scratch RTs (bank-aware via the stamped header).
        overlay_.bufferClearPipeline->BindPipeline(commandBuffer, &imageIndex);
        const VkExtent2D activeExtent = ActiveViewRenderExtent();
        vkCmdDispatch(
            commandBuffer,
            Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
            Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);

        // Only the primary view owns the swapchain image; secondary views must not clear it.
        if (!clearSwapchain)
        {
            return;
        }

        VkClearColorValue clearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};
        VkImageSubresourceRange imageRange = {};
        imageRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        imageRange.baseMipLevel = 0;
        imageRange.levelCount = 1;
        imageRange.baseArrayLayer = 0;
        imageRange.layerCount = 1;

        ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[imageIndex],
            0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(commandBuffer, SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clearColor, 1, &imageRange);
        ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[imageIndex],
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void VulkanBaseRenderer::DispatchVisibilityPass(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("visibility pass");

        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        auto& activeVisibilityPipeline = *overlay_.visibilityPipeline;
        FrameBuffer& visibilityFrameBuffer =
            activeVisibilityFrameBuffer_ ? *activeVisibilityFrameBuffer_ : *overlay_.visibilityFrameBuffer;
        renderPassInfo.renderPass = activeVisibilityPipeline.RenderPass().Handle();
        renderPassInfo.framebuffer = visibilityFrameBuffer.Handle();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = ActiveViewRenderExtent();
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            const auto& scene = GetScene();

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, activeVisibilityPipeline.Handle());
            const VkViewport viewport{
                0.0f,
                0.0f,
                static_cast<float>(renderPassInfo.renderArea.extent.width),
                static_cast<float>(renderPassInfo.renderArea.extent.height),
                0.0f,
                1.0f};
            const VkRect2D scissor{{0, 0}, renderPassInfo.renderArea.extent};
            vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
            vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
            const Assets::GPUScene& gpuScene = scene.FetchGPUScene(imageIndex);
            activeVisibilityPipeline.PipelineLayout().BindDescriptorSets(
                commandBuffer, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);
            vkCmdPushConstants(commandBuffer, activeVisibilityPipeline.PipelineLayout().Handle(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(Assets::GPUScene), &gpuScene);
            if (scene.GetIndirectDrawBatchCount() > 0)
            {
                vkCmdDrawIndirect(commandBuffer, scene.SoftMeshShaderDrawArgBuffer().Handle(), 0,
                                  1, sizeof(VkDrawIndirectCommand));
            }
        }
        vkCmdEndRenderPass(commandBuffer);

        // Copy the depth/id render target into the storage-image variant for later passes.
        VkImageCopy copyRegion;
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.srcOffset = {0, 0, 0};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstOffset = {0, 0, 0};
        copyRegion.extent = {GetViewStorageImage(Assets::Bindless::RT_MINIGBUFFER)->GetImage().Extent().width,
                             GetViewStorageImage(Assets::Bindless::RT_MINIGBUFFER)->GetImage().Extent().height, 1};

        GetViewStorageImage(Assets::Bindless::RT_MINIGBUFFER_DRAW)->InsertBarrier(commandBuffer,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        GetViewStorageImage(Assets::Bindless::RT_MINIGBUFFER)->InsertBarrier(commandBuffer,
            0, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        vkCmdCopyImage(commandBuffer,
            GetViewStorageImage(Assets::Bindless::RT_MINIGBUFFER_DRAW)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            GetViewStorageImage(Assets::Bindless::RT_MINIGBUFFER)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copyRegion);

        GetViewStorageImage(Assets::Bindless::RT_MINIGBUFFER)->InsertBarrier(commandBuffer,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
    }

    void VulkanBaseRenderer::DispatchSunShadow(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!overlay_.sunShadowPass || !GetScene().GetEnvSettings().HasSun)
        {
            return;
        }

        SCOPED_GPU_TIMER("shadow pass");
        auto& scene = GetScene();
        const uint32_t indirectDrawBatchCount = scene.GetIndirectDrawBatchCount();
        const uint32_t groupCount = (indirectDrawBatchCount + 63) / 64;
        const uint32_t activeCascadeMask = NextEngine::GetInstance()->GetSunShadowCascadeUpdateMask();
        Assets::GPUScene shadowGpuScene = scene.FetchGPUScene(imageIndex);

        if (activeCascadeMask != 0)
        {
            vkCmdFillBuffer(commandBuffer, scene.NodeMatrixBuffer().Handle(),
                            Assets::GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET,
                            sizeof(Assets::GPUDrivenStat) * Assets::Scene::kSunShadowCascadeCount, 0);

            VkBufferMemoryBarrier statsClearBarrier = {};
            statsClearBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            statsClearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            statsClearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            statsClearBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            statsClearBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            statsClearBarrier.buffer = scene.NodeMatrixBuffer().Handle();
            statsClearBarrier.offset = Assets::GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET;
            statsClearBarrier.size = sizeof(Assets::GPUDrivenStat) * Assets::Scene::kSunShadowCascadeCount;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 0, nullptr, 1, &statsClearBarrier, 0, nullptr);
        }

        if (activeCascadeMask != 0)
        {
            vkCmdFillBuffer(commandBuffer, scene.SoftMeshShaderCounterBuffer().Handle(), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(commandBuffer, scene.SoftMeshShaderDrawArgBuffer().Handle(), sizeof(VkDrawIndirectCommand),
                            sizeof(VkDrawIndirectCommand) * Assets::Scene::kSunShadowCascadeCount, 0);

            std::array<VkBufferMemoryBarrier, 2> clearBarriers{};
            clearBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            clearBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            clearBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            clearBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            clearBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            clearBarriers[0].buffer = scene.SoftMeshShaderCounterBuffer().Handle();
            clearBarriers[0].offset = 0;
            clearBarriers[0].size = VK_WHOLE_SIZE;
            clearBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            clearBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            clearBarriers[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            clearBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            clearBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            clearBarriers[1].buffer = scene.SoftMeshShaderDrawArgBuffer().Handle();
            clearBarriers[1].offset = sizeof(VkDrawIndirectCommand);
            clearBarriers[1].size = sizeof(VkDrawIndirectCommand) * Assets::Scene::kSunShadowCascadeCount;
            vkCmdPipelineBarrier(commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                0, 0, nullptr, static_cast<uint32_t>(clearBarriers.size()), clearBarriers.data(), 0, nullptr);
        }

        if (groupCount > 0 && activeCascadeMask != 0)
        {
            auto bindCompute = [&](const PipelineCommon::ZeroBindPipeline& pipeline)
            {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.Handle());
                pipeline.PipelineLayout().BindDescriptorSets(commandBuffer, 0);
            };

            shadowGpuScene.custom_data_0 = activeCascadeMask;
            shadowGpuScene.custom_data_1 = indirectDrawBatchCount;
            shadowGpuScene.custom_data_2 = scene.GetMaxSceneTriangles();

            bindCompute(*overlay_.shadowGpuCullCompactPipeline);
            vkCmdPushConstants(commandBuffer, overlay_.shadowGpuCullCompactPipeline->PipelineLayout().Handle(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &shadowGpuScene);
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);

            std::array<VkBufferMemoryBarrier, 2> compactBarriers{};
            compactBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            compactBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            compactBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            compactBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            compactBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            compactBarriers[0].buffer = scene.SoftMeshShaderCounterBuffer().Handle();
            compactBarriers[0].offset = 0;
            compactBarriers[0].size = VK_WHOLE_SIZE;
            compactBarriers[1] = compactBarriers[0];
            compactBarriers[1].buffer = scene.SoftMeshShaderVisibleItemBuffer().Handle();
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr,
                                 static_cast<uint32_t>(compactBarriers.size()), compactBarriers.data(),
                                 0, nullptr);

            bindCompute(*overlay_.softMeshShaderFinalizePipeline);
            for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
            {
                if ((activeCascadeMask & (1u << cascade)) == 0u)
                {
                    continue;
                }

                shadowGpuScene.custom_data_0 = scene.SoftMeshShaderDrawSlotForShadowCascade(cascade);
                vkCmdPushConstants(commandBuffer, overlay_.softMeshShaderFinalizePipeline->PipelineLayout().Handle(),
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &shadowGpuScene);
                vkCmdDispatch(commandBuffer, 1, 1, 1);
            }

            std::array<VkBufferMemoryBarrier, 2> expandInputBarriers{};
            expandInputBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            expandInputBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            expandInputBarriers[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            expandInputBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            expandInputBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            expandInputBarriers[0].buffer = scene.SoftMeshShaderDispatchArgBuffer().Handle();
            expandInputBarriers[0].offset = sizeof(VkDispatchIndirectCommand);
            expandInputBarriers[0].size = sizeof(VkDispatchIndirectCommand) * Assets::Scene::kSunShadowCascadeCount;
            expandInputBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            expandInputBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            expandInputBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            expandInputBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            expandInputBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            expandInputBarriers[1].buffer = scene.SoftMeshShaderVisibleItemBuffer().Handle();
            expandInputBarriers[1].offset = sizeof(Assets::SoftMeshShaderVisibleItem) * Assets::Scene::kMaxIndirectDrawCount;
            expandInputBarriers[1].size =
                sizeof(Assets::SoftMeshShaderVisibleItem) * Assets::Scene::kMaxIndirectDrawCount *
                Assets::Scene::kSunShadowCascadeCount;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 0, 0, nullptr, static_cast<uint32_t>(expandInputBarriers.size()),
                                 expandInputBarriers.data(), 0, nullptr);

            bindCompute(*overlay_.softMeshShaderExpandPipeline);
            for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
            {
                if ((activeCascadeMask & (1u << cascade)) == 0u)
                {
                    continue;
                }

                const uint32_t slot = scene.SoftMeshShaderDrawSlotForShadowCascade(cascade);
                shadowGpuScene.custom_data_0 = slot;
                vkCmdPushConstants(commandBuffer, overlay_.softMeshShaderExpandPipeline->PipelineLayout().Handle(),
                                   VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &shadowGpuScene);
                vkCmdDispatchIndirect(commandBuffer, scene.SoftMeshShaderDispatchArgBuffer().Handle(),
                                      sizeof(VkDispatchIndirectCommand) * slot);
            }

            std::array<VkBufferMemoryBarrier, 2> drawBarriers{};
            drawBarriers[0].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            drawBarriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            drawBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            drawBarriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            drawBarriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            drawBarriers[0].buffer = scene.SoftMeshShaderShadowPrimBuffer().Handle();
            drawBarriers[0].offset = 0;
            drawBarriers[0].size = VK_WHOLE_SIZE;
            drawBarriers[1].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            drawBarriers[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            drawBarriers[1].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            drawBarriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            drawBarriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            drawBarriers[1].buffer = scene.SoftMeshShaderDrawArgBuffer().Handle();
            drawBarriers[1].offset = sizeof(VkDrawIndirectCommand);
            drawBarriers[1].size = sizeof(VkDrawIndirectCommand) * Assets::Scene::kSunShadowCascadeCount;
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                                 0, 0, nullptr, static_cast<uint32_t>(drawBarriers.size()), drawBarriers.data(),
                                 0, nullptr);
        }

        for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
        {
            if ((activeCascadeMask & (1u << cascade)) == 0u)
            {
                continue;
            }

            overlay_.sunShadowPass->DrawCascade(
                commandBuffer, scene, shadowGpuScene, cascade);
        }
    }

    void VulkanBaseRenderer::DrawWireframeOverlay(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!overlay_.wireframePipeline || imageIndex >= overlay_.wireframeFrameBuffers.size())
        {
            SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
            return;
        }

        SCOPED_GPU_TIMER("wireframe");

        ImageMemoryBarrier::FullInsert(
            commandBuffer, SwapChain().Images()[imageIndex],
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT, 0,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        auto& activeWireframePipeline = *overlay_.wireframePipeline;
        renderPassInfo.renderPass = activeWireframePipeline.RenderPass().Handle();
        renderPassInfo.framebuffer = overlay_.wireframeFrameBuffers[imageIndex].Handle();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = SwapChain().Extent();
        renderPassInfo.clearValueCount = 0;
        renderPassInfo.pClearValues = nullptr;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            const auto& scene = GetScene();
            const Assets::GPUScene& gpuScene = scene.FetchGPUScene(imageIndex);

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, activeWireframePipeline.Handle());
            activeWireframePipeline.PipelineLayout().BindDescriptorSets(
                commandBuffer, 0, VK_PIPELINE_BIND_POINT_GRAPHICS);
            vkCmdPushConstants(commandBuffer, activeWireframePipeline.PipelineLayout().Handle(),
                               VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
            if (scene.GetIndirectDrawBatchCount() > 0)
            {
                vkCmdDrawIndirect(commandBuffer, scene.SoftMeshShaderDrawArgBuffer().Handle(), 0,
                                  1, sizeof(VkDrawIndirectCommand));
            }
        }
        vkCmdEndRenderPass(commandBuffer);
    }

    void VulkanBaseRenderer::DispatchVisualDebugger(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!VisualDebug())
        {
            return;
        }

        SCOPED_GPU_TIMER("visual debugger");
        SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);

        std::array<uint32_t, 5> pushConst = {
            imageIndex,
            uint32_t(SwapChain().OutputOffset().x), uint32_t(SwapChain().OutputOffset().y),
            uint32_t(SwapChain().OutputExtent().width), uint32_t(SwapChain().OutputExtent().height)
        };
        overlay_.visualDebuggerPipeline->BindPipeline(commandBuffer, pushConst.data());
        vkCmdDispatch(
            commandBuffer,
            Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().width, 8),
            Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().height, 8), 1);

        SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
    }

    void VulkanBaseRenderer::CopyObjectIdHistory(VkCommandBuffer commandBuffer)
    {
        SCOPED_GPU_TIMER("objectid copy");
        GetViewStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->InsertBarrier(commandBuffer,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        GetViewStorageImage(Assets::Bindless::RT_OBJEDCTID_1)->InsertBarrier(commandBuffer,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

        VkImageCopy copyRegion;
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.srcOffset = {0, 0, 0};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstOffset = {0, 0, 0};
        copyRegion.extent = {
            GetViewStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->GetImage().Extent().width,
            GetViewStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->GetImage().Extent().height, 1};

        vkCmdCopyImage(commandBuffer,
            GetViewStorageImage(Assets::Bindless::RT_OBJEDCTID_0)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            GetViewStorageImage(Assets::Bindless::RT_OBJEDCTID_1)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copyRegion);
    }
}
