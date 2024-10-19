#include "HybridDeferredRenderer.hpp"
#include "HybridDeferredPipeline.hpp"

#include "Vulkan/Buffer.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Utilities/Exception.hpp"
#include <array>

#include "Utilities/Console.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Vulkan/ModernDeferred/ModernDeferredPipeline.hpp"

namespace Vulkan::HybridDeferred
{
    HybridDeferredRenderer::HybridDeferredRenderer(RayTracing::RayTraceBaseRenderer& baseRender):LogicRendererBase(baseRender)
    {
    }

    HybridDeferredRenderer::~HybridDeferredRenderer()
    {
    }

    void HybridDeferredRenderer::CreateSwapChain()
    {
        const auto extent = SwapChain().Extent();
        const auto format = SwapChain().Format();

        visibilityPipeline0_.reset(new Vulkan::ModernDeferred::VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));
        visibilityPipeline1_.reset(new Vulkan::ModernDeferred::VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));
        
        rtVisibility0.reset(new RenderImage(Device(), extent,
                                            VK_FORMAT_R32G32_UINT,
                                            VK_IMAGE_TILING_OPTIMAL,
                                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,false,"visibility0"));

        rtVisibility1.reset(new RenderImage(Device(), extent,
                                            VK_FORMAT_R32G32_UINT,
                                            VK_IMAGE_TILING_OPTIMAL,
                                            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,false,"visibility1"));
        
        rtOutput.reset(new RenderImage(Device(), extent,
                                       VK_FORMAT_R16G16B16A16_SFLOAT,
                                       VK_IMAGE_TILING_OPTIMAL,
                                       VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));

        rtMotionVector.reset(new RenderImage(Device(), extent,
                                             VK_FORMAT_R16G16_SFLOAT,
                                             VK_IMAGE_TILING_OPTIMAL,
                                             VK_IMAGE_USAGE_STORAGE_BIT));

        rtAccumlation.reset(new RenderImage(Device(), extent,
                                            VK_FORMAT_R16G16B16A16_SFLOAT,
                                            VK_IMAGE_TILING_OPTIMAL,
                                            VK_IMAGE_USAGE_STORAGE_BIT));

        rtDirectLightSource.reset(new RenderImage(Device(), extent,
                                             VK_FORMAT_R16G16B16A16_SFLOAT,
                                             VK_IMAGE_TILING_OPTIMAL,
                                             VK_IMAGE_USAGE_STORAGE_BIT));

        rtDirectLightDest.reset(new RenderImage(Device(), extent,
                                             VK_FORMAT_R16G16B16A16_SFLOAT,
                                             VK_IMAGE_TILING_OPTIMAL,
                                             VK_IMAGE_USAGE_STORAGE_BIT));
        
        rtDirectLight0.reset(new RenderImage(Device(), extent,
                                             VK_FORMAT_R16G16B16A16_SFLOAT,
                                             VK_IMAGE_TILING_OPTIMAL,
                                             VK_IMAGE_USAGE_STORAGE_BIT));
        rtDirectLight1.reset(new RenderImage(Device(), extent,
                                             VK_FORMAT_R16G16B16A16_SFLOAT,
                                             VK_IMAGE_TILING_OPTIMAL,
                                             VK_IMAGE_USAGE_STORAGE_BIT));

        rtPingPong0.reset(new RenderImage(Device(), extent,
                                          VK_FORMAT_R16G16B16A16_SFLOAT,
                                          VK_IMAGE_TILING_OPTIMAL,
                                          VK_IMAGE_USAGE_STORAGE_BIT));

        rtPingPong1.reset(new RenderImage(Device(), extent,
                                          VK_FORMAT_R16G16B16A16_SFLOAT,
                                          VK_IMAGE_TILING_OPTIMAL,
                                          VK_IMAGE_USAGE_STORAGE_BIT));

        rtAlbedo_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT, true, "albedo"));
        rtNormal_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_LINEAR, VK_IMAGE_USAGE_STORAGE_BIT, true, "normal"));
        
        deferredFrameBuffer0_.reset(new FrameBuffer(rtVisibility0->GetImageView(), visibilityPipeline0_->RenderPass()));
        deferredFrameBuffer1_.reset(new FrameBuffer(rtVisibility1->GetImageView(), visibilityPipeline1_->RenderPass()));
        
        deferredShadingPipeline_.reset(new HybridShadingPipeline(SwapChain(), TLAS()[0],
                                                         rtVisibility0->GetImageView(),
                                                         rtVisibility1->GetImageView(),
                                                         rtAccumlation->GetImageView(),
                                                         rtMotionVector->GetImageView(),
                                                         rtDirectLightDest->GetImageView(),
                                                         rtDirectLightSource->GetImageView(),
                                                         rtAlbedo_->GetImageView(),
                                                         rtNormal_->GetImageView(),
                                                         UniformBuffers(), GetScene()));
        
        accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),
                                                                         rtAccumlation->GetImageView(),
                                                                         rtPingPong0->GetImageView(),
                                                                         rtPingPong1->GetImageView(),
                                                                         rtMotionVector->GetImageView(),
                                                                         rtVisibility0->GetImageView(),
                                                                         rtVisibility0->GetImageView(),
                                                                         rtOutput->GetImageView(),
                                                                         UniformBuffers(), GetScene()));

        accumulateForLightPipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),
                                                                         rtDirectLightSource->GetImageView(),
                                                                         rtDirectLight0->GetImageView(),
                                                                         rtDirectLight1->GetImageView(),
                                                                         rtMotionVector->GetImageView(),
                                                                         rtVisibility0->GetImageView(),
                                                                         rtVisibility1->GetImageView(),
                                                                         rtDirectLightDest->GetImageView(),
                                                                         UniformBuffers(), GetScene()));

        composePipeline_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), rtOutput->GetImageView(), rtAlbedo_->GetImageView(),  rtNormal_->GetImageView(), rtVisibility0->GetImageView(), rtVisibility0->GetImageView(), UniformBuffers()));

        visualDebugPipeline_.reset(new PipelineCommon::VisualDebuggerPipeline(SwapChain(),
                                                              rtAccumlation->GetImageView(), rtMotionVector->GetImageView(), rtVisibility0->GetImageView(), rtOutput->GetImageView(),
                                                              UniformBuffers()));
    }

    void HybridDeferredRenderer::DeleteSwapChain()
    {
        visibilityPipeline0_.reset();
        visibilityPipeline1_.reset();
        
        deferredShadingPipeline_.reset();
        accumulatePipeline_.reset();
        accumulateForLightPipeline_.reset();
        composePipeline_.reset();
        visualDebugPipeline_.reset();
        
        deferredFrameBuffer0_.reset();
        deferredFrameBuffer1_.reset();

        rtVisibility0.reset();
        rtVisibility1.reset();
        
        rtOutput.reset();
        rtMotionVector.reset();

        rtAccumlation.reset();
        rtDirectLight0.reset();
        rtDirectLight1.reset();
        rtDirectLightSource.reset();
        rtDirectLightDest.reset();

        rtPingPong0.reset();
        rtPingPong1.reset();

        rtAlbedo_.reset();
        rtNormal_.reset();
    }

    void HybridDeferredRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        //rtVisibility0->InsertBarrier(commandBuffer, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = {{0, 0, 0, 0}};
        clearValues[1].depthStencil = {1.0f, 0};
        
        {
            SCOPED_GPU_TIMER("drawpass");

            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            if(FrameCount() % 2 == 0)
            {
                renderPassInfo.renderPass = visibilityPipeline0_->RenderPass().Handle();
                renderPassInfo.framebuffer = deferredFrameBuffer0_->Handle();  
            }
            else
            {
                renderPassInfo.renderPass = visibilityPipeline1_->RenderPass().Handle();
                renderPassInfo.framebuffer = deferredFrameBuffer1_->Handle();
            }

            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = SwapChain().Extent();
            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();

            // make it to generate gbuffer
            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            {
            const auto& scene = GetScene();

            VkDescriptorSet descriptorSets[] = { (FrameCount() % 2 == 0 ? visibilityPipeline0_ : visibilityPipeline1_)->DescriptorSet(imageIndex)};
            VkBuffer vertexBuffers[] = {scene.VertexBuffer().Handle()};
            const VkBuffer indexBuffer = scene.IndexBuffer().Handle();
            VkDeviceSize offsets[] = {0};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, (FrameCount() % 2 == 0 ? visibilityPipeline0_ : visibilityPipeline1_)->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, (FrameCount() % 2 == 0 ? visibilityPipeline0_ : visibilityPipeline1_)->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

            // indirect draw
            vkCmdDrawIndexedIndirect(commandBuffer, scene.IndirectDrawBuffer().Handle(), 0, scene.GetIndirectDrawBatchCount(), sizeof(VkDrawIndexedIndirectCommand));
            }
            vkCmdEndRenderPass(commandBuffer);

            (FrameCount() % 2 == 0 ? rtVisibility0 : rtVisibility1)->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);
            (FrameCount() % 2 == 1 ? rtVisibility0 : rtVisibility1)->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            rtOutput->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            rtMotionVector->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            rtAccumlation->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            rtPingPong0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            rtPingPong1->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            rtDirectLight0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            rtDirectLight1->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            rtDirectLightSource->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            rtDirectLightDest->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            rtAlbedo_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
            rtNormal_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            SCOPED_GPU_TIMER("shadingpass");
            // cs shading pass
            VkDescriptorSet DescriptorSets[] = {deferredShadingPipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    deferredShadingPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);

            // bind the global bindless set
            static const uint32_t k_bindless_set = 1;
            VkDescriptorSet GlobalDescriptorSets[] = { Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0) };
            vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->PipelineLayout().Handle(), k_bindless_set,
                                     1, GlobalDescriptorSets, 0, nullptr );
            
            uint32_t workGroupSizeXDivider = 8;
            uint32_t workGroupSizeYDivider = 4;
#if ANDROID
            workGroupSizeXDivider = 32;
            workGroupSizeYDivider = 32;
#endif
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().width, workGroupSizeXDivider),
                          Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().height, workGroupSizeYDivider), 1);

            rtAccumlation->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }

        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;
        {
            SCOPED_GPU_TIMER("reproject pass");
            VkDescriptorSet DescriptorSets[] = {accumulatePipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    accumulatePipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().height, 4), 1);

            rtOutput->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            SCOPED_GPU_TIMER("dlight reproj pass");
            VkDescriptorSet DescriptorSets[] = {accumulateForLightPipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulateForLightPipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    accumulateForLightPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().Extent().height, 4), 1);
        }

        {
            SCOPED_GPU_TIMER("compose pass");
            
            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_GENERAL);
            
            VkDescriptorSet DescriptorSets[] = {composePipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    composePipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 16, SwapChain().Extent().height / 16, 1);

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }

        if (VisualDebug())
        {
            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                       VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, rtDirectLight0->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, rtMotionVector->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, rtOutput->GetImage().Handle(), subresourceRange, VK_ACCESS_SHADER_WRITE_BIT,
                                       VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                                       VK_IMAGE_LAYOUT_GENERAL);
        
            {
                VkDescriptorSet DescriptorSets[] = {visualDebugPipeline_->DescriptorSet(imageIndex)};
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, visualDebugPipeline_->Handle());
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                        visualDebugPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
                vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);
            }
        
            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                                       VK_ACCESS_TRANSFER_WRITE_BIT,
                                       0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
    }
}
