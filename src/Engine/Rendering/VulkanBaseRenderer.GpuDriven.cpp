// VulkanBaseRenderer GPU-driven frame dispatch: skinning, culling,
// visibility, sun shadow, wireframe overlay and visual debugger passes.
// Split from VulkanBaseRenderer.cpp; same class, separate TU.
#include "Engine/Rendering/VulkanBaseRenderer.hpp"

// GPU-driven pass implementation belongs to the concrete renderer feature pack.
#include "Engine/Common/CoreMinimal.hpp"
#include "Engine/Assets/Core/Node.hpp"
#include "Engine/Assets/Core/Scene.hpp"
#include "Engine/Assets/GPU/Texture.hpp"
#include "Engine/Assets/GPU/UniformBuffer.hpp"
#include "Engine/Options.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Rendering/PipelineCommon/VisibilityBufferLayout.hpp"
#include "Engine/Rendering/Shadow/ShadowMapPass.hpp"
#include "Engine/Runtime/Components/RenderComponent.hpp"
#include "Engine/Runtime/Components/SkinnedMeshComponent.hpp"
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
#include "Engine/Runtime/Profiling/ProfilerMacros.hpp"

namespace Vulkan
{
    void VulkanBaseRenderer::DispatchSkinning(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("skinning pass");
        auto& scene = GetScene();

        skin_.pipeline->BindPipeline(commandBuffer, scene, imageIndex);

        Assets::GPUScene gpuScene = scene.FetchGPUScene(imageIndex, ActiveViewBankBase());
        Vulkan::Buffer* skinnedVertexBuffer = scene.SkinnedVertexBuffer();
        if (!skinnedVertexBuffer)
        {
            return;
        }

        for (const uint32_t modelId : scene.SkinUpdateRequests())
        {
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
            uint32_t skinnedProxyModelId = 0;
            if (!Assets::Scene::TryEncodeModelSection(modelId, 0, skinnedProxyModelId) ||
                skinnedProxyModelId >= scene.Offsets().size())
            {
                SPDLOG_ERROR("Skipping skinning for model {}: invalid encoded model offset", modelId);
                continue;
            }
            const auto& nodeProxies = scene.GetNodeProxies();
            for (uint32_t nodeIdx = 0; nodeIdx < nodeProxies.size(); ++nodeIdx)
            {
                if (nodeProxies[nodeIdx].modelId == skinnedProxyModelId)
                {
                    proxyIdx = nodeIdx;
                    break;
                }
            }
            if (proxyIdx == std::numeric_limits<uint32_t>::max())
            {
                continue;
            }

            uint32_t vertexOffset = scene.Offsets()[skinnedProxyModelId].vertexOffset;
            uint32_t vertexCount = model->NumberOfVertices();

            gpuScene.CustomData0 = proxyIdx;
            gpuScene.CustomData1 = vertexOffset;
            gpuScene.CustomData2 = vertexCount;

            vkCmdPushConstants(commandBuffer, skin_.pipeline->PipelineLayout().Handle(),
                               VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Assets::GPUScene), &gpuScene);
            uint32_t groupCount = (vertexCount + 63) / 64;
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);
        }

        VkAccessFlags skinnedDstAccess = VK_ACCESS_SHADER_READ_BIT;
        VkPipelineStageFlags skinnedDstStages =
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        if (caps_.supportRayTracing)
        {
            skinnedDstAccess |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
            skinnedDstStages |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        }
        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, skinnedDstStages,
                                    skinnedVertexBuffer->Handle(), VK_ACCESS_SHADER_WRITE_BIT, skinnedDstAccess);
    }

    void VulkanBaseRenderer::DispatchLightGridBuild(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        auto& scene = GetScene();
        if (!scene.HasLightGridBuffer() ||
            Assets::ResolveLightGridCascadeCount(
                FrameSettings().userSettings.LightGridCascadeCount, scene.GetLightCount(),
                ActiveRendererRequirements().requestLightGrid) == 0)
        {
            return;
        }

        SCOPED_GPU_TIMER("light grid build");

        // The grid is rebuilt from scratch every frame, so the only ordering that matters is
        // against last frame's readers in the shading passes.
        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                    scene.LightGridBuffer().Handle(),
                                    VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);

        const Assets::GPUScene gpuScene = scene.FetchGPUScene(imageIndex, ActiveViewBankBase());
        overlay_.lightGridBuildPipeline->BindPipeline(commandBuffer, gpuScene);
        vkCmdDispatch(commandBuffer,
                      Utilities::Math::GetSafeDispatchCount(Assets::GPU_SCENE_LIGHT_GRID_CELLS, 64),
                      1, 1);

        BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                    scene.LightGridBuffer().Handle(),
                                    VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    void VulkanBaseRenderer::DispatchGpuCulling(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead},
        }, "gpu culling depth input");
        const uint32_t indirectDrawBatchCount = GetScene().GetIndirectDrawBatchCount();
        if (indirectDrawBatchCount == 0)
        {
            return;
        }

        auto& scene = GetScene();
        Assets::GPUScene gpuScene = scene.FetchGPUScene(imageIndex, ActiveViewBankBase());
        const uint32_t maxSceneTriangles = scene.GetMaxSceneTriangles();
        
        {
            SCOPED_GPU_TIMER("MShader cull");
            BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                            GetScene().NodeMatrixBuffer().Handle(), VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);

            // Scene-global soft-mesh scratch is reused by every scheduled view. Complete the
            // previous view's fill/compute/indirect consumers before discarding it here.
            BufferMemoryBarrier::Insert(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                    VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                scene.SoftMeshShaderCounterBuffer().Handle(),
                VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
                VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                VK_ACCESS_TRANSFER_WRITE_BIT);

            BufferMemoryBarrier::Insert(
                commandBuffer,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                {
                    BufferMemoryBarrier::Make(
                        scene.SoftMeshShaderVisibleItemBuffer().Handle(),
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT),
                    BufferMemoryBarrier::Make(
                        scene.SoftMeshShaderDispatchArgBuffer().Handle(),
                        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT),
                    BufferMemoryBarrier::Make(
                        scene.SoftMeshShaderPrimBuffer().Handle(),
                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT),
                    BufferMemoryBarrier::Make(
                        scene.SoftMeshShaderDrawArgBuffer().Handle(),
                        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                        VK_ACCESS_SHADER_WRITE_BIT),
                });

            vkCmdFillBuffer(commandBuffer, scene.SoftMeshShaderCounterBuffer().Handle(), 0, VK_WHOLE_SIZE, 0);

            BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                        scene.SoftMeshShaderCounterBuffer().Handle(), VK_ACCESS_TRANSFER_WRITE_BIT,
                                        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            
            overlay_.gpuCullCompactPipeline->BindPipeline(
                commandBuffer, gpuScene, gpuScene.CustomData0, indirectDrawBatchCount, maxSceneTriangles);
            uint32_t groupCount = (indirectDrawBatchCount + 63) / 64;
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);

            BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
                BufferMemoryBarrier::Make(scene.SoftMeshShaderCounterBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                BufferMemoryBarrier::Make(scene.SoftMeshShaderVisibleItemBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
            });
        }
        
        {
            SCOPED_GPU_TIMER("MShader finalize");
            overlay_.softMeshShaderFinalizePipeline->BindPipeline(
    commandBuffer, gpuScene, 0, indirectDrawBatchCount, maxSceneTriangles);
            vkCmdDispatch(commandBuffer, 1, 1, 1);

            BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
                BufferMemoryBarrier::Make(scene.SoftMeshShaderDispatchArgBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, 0, sizeof(VkDispatchIndirectCommand)),
                BufferMemoryBarrier::Make(scene.SoftMeshShaderVisibleItemBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 0, sizeof(Assets::SoftMeshShaderVisibleItem) * Assets::Scene::kRenderProxyCapacity),
            });
            
            overlay_.softMeshShaderExpandPipeline->BindPipeline(
commandBuffer, gpuScene, 0, indirectDrawBatchCount, maxSceneTriangles);
            vkCmdDispatchIndirect(commandBuffer, scene.SoftMeshShaderDispatchArgBuffer().Handle(), 0);

            BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, {
                BufferMemoryBarrier::Make(scene.SoftMeshShaderPrimBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                BufferMemoryBarrier::Make(scene.SoftMeshShaderDrawArgBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT, 0, sizeof(VkDrawIndirectCommand)),
            });
        }
    }

    void VulkanBaseRenderer::DispatchClearPass(VkCommandBuffer commandBuffer, uint32_t imageIndex, bool clearSwapchain)
    {
        SCOPED_GPU_TIMER("clear pass");

        TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderWrite, VK_IMAGE_LAYOUT_GENERAL, true},
            {Assets::Bindless::RT_SINGLE_SPECULAR, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderWrite, VK_IMAGE_LAYOUT_GENERAL, true},
            {Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderWrite, VK_IMAGE_LAYOUT_GENERAL, true},
        }, "clear view buffers");

        // Clears the active view's screen-space scratch RTs (bank-aware via the stamped header).
        overlay_.bufferClearPipeline->BindPipeline(commandBuffer, &imageIndex, ActiveViewBankBase());
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

        TransitionSwapchainImage(
            commandBuffer, imageIndex,
            {.stages = PipelineCommon::ERenderStage::Transfer,
             .access = PipelineCommon::EResourceAccess::TransferWrite,
             .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
             .discardPreviousContents = true},
            "clear swapchain");
        vkCmdClearColorImage(commandBuffer, SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                             &clearColor, 1, &imageRange);
        TransitionSwapchainImage(
            commandBuffer, imageIndex,
            {.stages = PipelineCommon::ERenderStage::Transfer,
             .access = PipelineCommon::EResourceAccess::TransferWrite,
             .layout = VK_IMAGE_LAYOUT_GENERAL},
            "clear swapchain steady state");
    }

    void VulkanBaseRenderer::DispatchVisibilityPass(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        SCOPED_GPU_TIMER("visibility pass");

        const uint32_t viewBase = ActiveViewBankBase();
        const auto emitTrackedBarrier = [commandBuffer](
            VkImage image, const PipelineCommon::FImageBarrier& barrier)
        {
            ImageMemoryBarrier::Insert(commandBuffer, barrier.srcStages, barrier.dstStages,
                                       image, barrier.range, barrier.srcAccess, barrier.dstAccess,
                                       barrier.oldLayout, barrier.newLayout);
        };

        std::array<VkClearValue, 3> clearValues = {};
        clearValues[0].color.uint32[0] = 0u;
        clearValues[1].color.uint32[0] = 0u;
        clearValues[2].depthStencil = {0.0f, 0};

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        auto& activeVisibilityPipeline = *overlay_.visibilityPipeline;
        FrameBuffer& visibilityFrameBuffer = activeViewContext_.visibilityFramebuffer
            ? *activeViewContext_.visibilityFramebuffer : *overlay_.visibilityFrameBuffer;
        renderPassInfo.renderPass = activeVisibilityPipeline.RenderPass().Handle();
        renderPassInfo.framebuffer = visibilityFrameBuffer.Handle();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = ActiveViewRenderExtent();
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        // The depth attachment is swapchain-local and intentionally reused by sequential views.
        // Its render pass discards prior contents, but discard does not remove the execution and
        // memory dependency on the preceding view's depth writes.
        VkMemoryBarrier depthReuseBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        depthReuseBarrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depthReuseBarrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                             0, 1, &depthReuseBarrier, 0, nullptr, 0, nullptr);

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
            const Assets::GPUScene& gpuScene = scene.FetchGPUScene(imageIndex, ActiveViewBankBase());
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

        // The render pass explicitly clears/discards both attachments and leaves them in
        // PRESENT_SRC. Track each physical plane independently for the transfer copies below.
        for (const PipelineCommon::FVisibilityPlaneDesc& plane : PipelineCommon::kVisibilityPlanes)
        {
            const uint32_t drawId = viewBase + plane.drawSlot;
            const PipelineCommon::FImageHandle drawHandle{static_cast<uint64_t>(drawId) + 1u};
            visibilityStateTracker_.Import(drawHandle, {
                .initialized = true,
                .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .stages = PipelineCommon::ERenderStage::ColorAttachment,
                .access = PipelineCommon::EResourceAccess::ColorWrite,
                .lastPass = "visibility render pass",
            });
        }

        // Copy the depth/id render target into the storage-image variant for later passes.
        VkImageCopy copyRegion;
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.srcOffset = {0, 0, 0};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstOffset = {0, 0, 0};
        const auto* extentImage = GetViewStorageImage(PipelineCommon::kVisibilityPlanes[0].storageSlot);
        copyRegion.extent = {extentImage->GetImage().Extent().width,
                             extentImage->GetImage().Extent().height, 1};

        for (const PipelineCommon::FVisibilityPlaneDesc& plane : PipelineCommon::kVisibilityPlanes)
        {
            const uint32_t drawId = viewBase + plane.drawSlot;
            const uint32_t storageId = viewBase + plane.storageSlot;
            const PipelineCommon::FImageHandle drawHandle{static_cast<uint64_t>(drawId) + 1u};
            const PipelineCommon::FImageHandle storageHandle{static_cast<uint64_t>(storageId) + 1u};
            const auto* drawImage = GetViewStorageImage(plane.drawSlot);
            const auto* storageImage = GetViewStorageImage(plane.storageSlot);

            if (const auto barrier = visibilityStateTracker_.Use(
                    {.image = drawHandle,
                     .stages = PipelineCommon::ERenderStage::Transfer,
                     .access = PipelineCommon::EResourceAccess::TransferRead,
                     .layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                    "visibility copy source"))
            {
                emitTrackedBarrier(drawImage->GetImage().Handle(), *barrier);
            }
            if (const auto barrier = visibilityStateTracker_.Use(
                    {.image = storageHandle,
                     .stages = PipelineCommon::ERenderStage::Transfer,
                     .access = PipelineCommon::EResourceAccess::TransferWrite,
                     .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     .discardPreviousContents = true},
                    "visibility copy destination"))
            {
                emitTrackedBarrier(storageImage->GetImage().Handle(), *barrier);
            }

            vkCmdCopyImage(commandBuffer,
                drawImage->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                storageImage->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &copyRegion);

            if (const auto barrier = visibilityStateTracker_.Use(
                    {.image = storageHandle,
                     .stages = PipelineCommon::ERenderStage::Compute,
                     .access = PipelineCommon::EResourceAccess::ShaderRead,
                     .layout = VK_IMAGE_LAYOUT_GENERAL},
                    "visibility compute read"))
            {
                emitTrackedBarrier(storageImage->GetImage().Handle(), *barrier);
            }
        }
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
        // Reference views deliberately share the primary camera and shadow-map set. Their own
        // RenderView state is only temporal image history; using it for CSM scheduling leaves the
        // update mask at its initial zero value after the first reference frame.
        const bool sharedPrimaryShadow = GOption != nullptr && GOption->ReferenceMode;
        const FViewRenderState& viewState = sharedPrimaryShadow
            ? PrimaryView().State()
            : ActiveRenderView().State();
        uint32_t activeCascadeMask = viewState.sunShadowCascadeUpdateMask;
        const bool sameCameraFamily = sharedPrimaryShadow || (shadowCameraFamilyCache_.valid &&
            shadowCameraFamilyCache_.scene == &scene &&
            shadowCameraFamilyCache_.sceneGeneration == SceneGeneration() &&
            std::memcmp(&shadowCameraFamilyCache_.cascades, &viewState.cachedSunCascades,
                        sizeof(Assets::CascadeShadowSetup)) == 0);
        if (!sameCameraFamily)
        {
            // A shared shadow image set currently contains another camera family's cascades.
            // Refresh every image so matrices and sampled maps cannot be mixed.
            activeCascadeMask = Assets::Scene::kSunShadowCascadeMask;
        }
        else if (shadowCameraFamilyCache_.renderedFrame == static_cast<uint64_t>(FrameCount()))
        {
            // Reference views with the same camera family reuse the set rendered earlier this frame.
            activeCascadeMask = 0;
        }
        Assets::GPUScene shadowGpuScene = scene.FetchGPUScene(imageIndex, ActiveViewBankBase());

        if (activeCascadeMask != 0)
        {
            // Shadow scratch is scene-global and reused after the main cull as well as by every
            // scheduled RenderView. Finish the previous fill/compute/indirect consumers before
            // clearing the per-cascade statistics and arguments for this shadow pass.
            BufferMemoryBarrier::Insert(
                commandBuffer,
                VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT |
                    VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
                    VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                {
                    BufferMemoryBarrier::Make(
                        scene.NodeMatrixBuffer().Handle(),
                        VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT |
                            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                        Assets::GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET,
                        sizeof(Assets::GPUDrivenStat) * Assets::Scene::kSunShadowCascadeCount),
                    BufferMemoryBarrier::Make(
                        scene.SoftMeshShaderCounterBuffer().Handle(),
                        VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_SHADER_WRITE_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT),
                    BufferMemoryBarrier::Make(
                        scene.SoftMeshShaderDrawArgBuffer().Handle(),
                        VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT |
                            VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                        VK_ACCESS_TRANSFER_WRITE_BIT,
                        sizeof(VkDrawIndirectCommand),
                        sizeof(VkDrawIndirectCommand) * Assets::Scene::kSunShadowCascadeCount),
                });

            vkCmdFillBuffer(commandBuffer, scene.NodeMatrixBuffer().Handle(),
                            Assets::GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET,
                            sizeof(Assets::GPUDrivenStat) * Assets::Scene::kSunShadowCascadeCount, 0);

            BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                scene.NodeMatrixBuffer().Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                Assets::GPU_SCENE_DYNAMIC_SHADOW_GPU_DRIVEN_STATS_OFFSET, sizeof(Assets::GPUDrivenStat) * Assets::Scene::kSunShadowCascadeCount);
        }

        if (activeCascadeMask != 0)
        {
            vkCmdFillBuffer(commandBuffer, scene.SoftMeshShaderCounterBuffer().Handle(), 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(commandBuffer, scene.SoftMeshShaderDrawArgBuffer().Handle(), sizeof(VkDrawIndirectCommand),
                            sizeof(VkDrawIndirectCommand) * Assets::Scene::kSunShadowCascadeCount, 0);

            BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, {
                BufferMemoryBarrier::Make(scene.SoftMeshShaderCounterBuffer().Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
                BufferMemoryBarrier::Make(scene.SoftMeshShaderDrawArgBuffer().Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                          sizeof(VkDrawIndirectCommand), sizeof(VkDrawIndirectCommand) * Assets::Scene::kSunShadowCascadeCount),
            });
        }

        if (groupCount > 0 && activeCascadeMask != 0)
        {
            const uint32_t maxSceneTriangles = scene.GetMaxSceneTriangles();

            overlay_.shadowGpuCullCompactPipeline->BindPipeline(
                commandBuffer, shadowGpuScene, activeCascadeMask, indirectDrawBatchCount, maxSceneTriangles);
            vkCmdDispatch(commandBuffer, groupCount, 1, 1);

            BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
                BufferMemoryBarrier::Make(scene.SoftMeshShaderCounterBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                BufferMemoryBarrier::Make(scene.SoftMeshShaderVisibleItemBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
            });

            for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
            {
                if ((activeCascadeMask & (1u << cascade)) == 0u)
                {
                    continue;
                }

                const uint32_t slot = scene.SoftMeshShaderDrawSlotForShadowCascade(cascade);
                overlay_.softMeshShaderFinalizePipeline->BindPipeline(
                    commandBuffer, shadowGpuScene, slot, indirectDrawBatchCount, maxSceneTriangles);
                vkCmdDispatch(commandBuffer, 1, 1, 1);
            }

            BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {
                BufferMemoryBarrier::Make(scene.SoftMeshShaderDispatchArgBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                                          sizeof(VkDispatchIndirectCommand), sizeof(VkDispatchIndirectCommand) * Assets::Scene::kSunShadowCascadeCount),
                BufferMemoryBarrier::Make(scene.SoftMeshShaderVisibleItemBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                          sizeof(Assets::SoftMeshShaderVisibleItem) * Assets::Scene::kRenderProxyCapacity,
                                          sizeof(Assets::SoftMeshShaderVisibleItem) * Assets::Scene::kRenderProxyCapacity * Assets::Scene::kSunShadowCascadeCount),
            });

            for (uint32_t cascade = 0; cascade < Assets::Scene::kSunShadowCascadeCount; ++cascade)
            {
                if ((activeCascadeMask & (1u << cascade)) == 0u)
                {
                    continue;
                }

                const uint32_t slot = scene.SoftMeshShaderDrawSlotForShadowCascade(cascade);
                overlay_.softMeshShaderExpandPipeline->BindPipeline(
                    commandBuffer, shadowGpuScene, slot, indirectDrawBatchCount, maxSceneTriangles);
                vkCmdDispatchIndirect(commandBuffer, scene.SoftMeshShaderDispatchArgBuffer().Handle(),
                                      sizeof(VkDispatchIndirectCommand) * slot);
            }

            BufferMemoryBarrier::Insert(commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT, {
                BufferMemoryBarrier::Make(scene.SoftMeshShaderShadowPrimBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT),
                BufferMemoryBarrier::Make(scene.SoftMeshShaderDrawArgBuffer().Handle(), VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_INDIRECT_COMMAND_READ_BIT,
                                          sizeof(VkDrawIndirectCommand), sizeof(VkDrawIndirectCommand) * Assets::Scene::kSunShadowCascadeCount),
            });
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
        shadowCameraFamilyCache_ = {
            .valid = true,
            .scene = &scene,
            .sceneGeneration = SceneGeneration(),
            .renderedFrame = static_cast<uint64_t>(FrameCount()),
            .cascades = viewState.cachedSunCascades,
        };
    }

    void VulkanBaseRenderer::DrawWireframeOverlay(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        if (!overlay_.wireframePipeline || imageIndex >= overlay_.wireframeFrameBuffers.size())
        {
            return;
        }

        SCOPED_GPU_TIMER("wireframe");

        TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_SCENE_COLOR, PipelineCommon::ERenderStage::ColorAttachment,
             PipelineCommon::EResourceAccess::ColorRead | PipelineCommon::EResourceAccess::ColorWrite},
        }, "wireframe overlay");

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        auto& activeWireframePipeline = *overlay_.wireframePipeline;
        renderPassInfo.renderPass = activeWireframePipeline.RenderPass().Handle();
        renderPassInfo.framebuffer = overlay_.wireframeFrameBuffers[imageIndex].Handle();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = SwapChain().RenderExtent();
        renderPassInfo.clearValueCount = 0;
        renderPassInfo.pClearValues = nullptr;

        vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
        {
            const auto& scene = GetScene();
            const Assets::GPUScene& gpuScene = scene.FetchGPUScene(imageIndex, ActiveViewBankBase());

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
        constexpr uint32_t outputIndex = Assets::Bindless::RT_TONEMAP_OUTPUT;
        RenderImage* intermediateOutput = lateToneMapping_.outputImage.get();
        if (intermediateOutput == nullptr)
        {
            SPDLOG_ERROR("Visual debugger has no intermediate output");
            return;
        }

        constexpr VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        const bool initialized = lateToneMapping_.outputInitialized;
        ImageMemoryBarrier::Insert(
            commandBuffer,
            initialized ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT
                        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            intermediateOutput->GetImage().Handle(),
            range,
            initialized ? VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT : 0u,
            VK_ACCESS_SHADER_WRITE_BIT,
            initialized ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_GENERAL);
        lateToneMapping_.outputInitialized = true;

        std::array<uint32_t, 5> pushConst = {
            outputIndex,
            uint32_t(SwapChain().OutputOffset().x), uint32_t(SwapChain().OutputOffset().y),
            uint32_t(SwapChain().OutputExtent().width), uint32_t(SwapChain().OutputExtent().height)
        };
        overlay_.visualDebuggerPipeline->BindPipeline(commandBuffer, pushConst.data(), ActiveViewBankBase());
        vkCmdDispatch(
            commandBuffer,
            Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().width, 8),
            Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().height, 8), 1);

        ImageMemoryBarrier::Insert(
            commandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            intermediateOutput->GetImage().Handle(),
            range,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_GENERAL);
        TransitionSwapchainImage(
            commandBuffer, imageIndex,
            {.stages = PipelineCommon::ERenderStage::Transfer,
             .access = PipelineCommon::EResourceAccess::TransferWrite,
             .layout = VK_IMAGE_LAYOUT_GENERAL},
            "visual debugger blit");
        VkImageBlit blitRegion{};
        blitRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.srcOffsets[0] = {0, 0, 0};
        blitRegion.srcOffsets[1] = {
            static_cast<int32_t>(SwapChain().OutputExtent().width),
            static_cast<int32_t>(SwapChain().OutputExtent().height),
            1};
        blitRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blitRegion.dstOffsets[0] = {
            SwapChain().OutputOffset().x,
            SwapChain().OutputOffset().y,
            0};
        blitRegion.dstOffsets[1] = {
            SwapChain().OutputOffset().x + static_cast<int32_t>(SwapChain().OutputExtent().width),
            SwapChain().OutputOffset().y + static_cast<int32_t>(SwapChain().OutputExtent().height),
            1};
        vkCmdBlitImage(
            commandBuffer,
            intermediateOutput->GetImage().Handle(),
            VK_IMAGE_LAYOUT_GENERAL,
            SwapChain().Images()[imageIndex],
            VK_IMAGE_LAYOUT_GENERAL,
            1,
            &blitRegion,
            VK_FILTER_NEAREST);

        TransitionSwapchainImage(
            commandBuffer, imageIndex,
            {.stages = PipelineCommon::ERenderStage::Present,
             .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR},
            "visual debugger present");
    }

    void VulkanBaseRenderer::CopyObjectIdHistory(VkCommandBuffer commandBuffer)
    {
        SCOPED_GPU_TIMER("objectid copy");
        TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_OBJECTID_0, PipelineCommon::ERenderStage::Transfer,
             PipelineCommon::EResourceAccess::TransferRead, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
            {Assets::Bindless::RT_OBJECTID_1, PipelineCommon::ERenderStage::Transfer,
             PipelineCommon::EResourceAccess::TransferWrite, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL},
        }, "object id history copy");

        VkImageCopy copyRegion;
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.srcOffset = {0, 0, 0};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstOffset = {0, 0, 0};
        copyRegion.extent = {
            GetViewStorageImage(Assets::Bindless::RT_OBJECTID_0)->GetImage().Extent().width,
            GetViewStorageImage(Assets::Bindless::RT_OBJECTID_0)->GetImage().Extent().height, 1};

        vkCmdCopyImage(commandBuffer,
            GetViewStorageImage(Assets::Bindless::RT_OBJECTID_0)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            GetViewStorageImage(Assets::Bindless::RT_OBJECTID_1)->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &copyRegion);

        TransitionActiveViewImages(commandBuffer, {
            {Assets::Bindless::RT_OBJECTID_0, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead | PipelineCommon::EResourceAccess::ShaderWrite},
            {Assets::Bindless::RT_OBJECTID_1, PipelineCommon::ERenderStage::Compute,
             PipelineCommon::EResourceAccess::ShaderRead | PipelineCommon::EResourceAccess::ShaderWrite},
        }, "object id history ready");
    }
}
