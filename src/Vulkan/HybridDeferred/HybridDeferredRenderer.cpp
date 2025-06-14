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
#include "Vulkan/DepthBuffer.hpp"
#include "Vulkan/DescriptorSetManager.hpp"
#include "Vulkan/DescriptorSets.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Vulkan/ModernDeferred/ModernDeferredPipeline.hpp"

namespace Vulkan::HybridDeferred
{
    HybridDeferredRenderer::HybridDeferredRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
    {
    }

    HybridDeferredRenderer::~HybridDeferredRenderer()
    {
    }

    void HybridDeferredRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        const auto format = SwapChain().Format();
        
        visibilityPipeline0_.reset(new Vulkan::ModernDeferred::VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));

        rtPingPong0.reset(new RenderImage(Device(), extent,
                                          VK_FORMAT_R16G16B16A16_SFLOAT,
                                          VK_IMAGE_TILING_OPTIMAL,
                                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT));

        deferredFrameBuffer0_.reset(new FrameBuffer(extent, baseRender_.rtVisibility->GetImageView(), visibilityPipeline0_->RenderPass()));
        
        deferredShadingPipeline_.reset(new HybridShadingPipeline(SwapChain(), GetBaseRender<RayTracing::RayTraceBaseRenderer>().TLAS()[0],
                                                         baseRender_,UniformBuffers(), GetScene()));
        
        accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),baseRender_,rtPingPong0->GetImageView(),UniformBuffers(), GetScene()));

        composePipeline_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), baseRender_, UniformBuffers()));

        visualDebugPipeline_.reset(new PipelineCommon::VisualDebuggerPipeline(SwapChain(),
                                                              baseRender_.rtAccumlation->GetImageView(), baseRender_.rtNormal_->GetImageView(), baseRender_.rtObject0->GetImageView(), baseRender_.rtObject1->GetImageView(),
                                                              UniformBuffers()));
    }

    void HybridDeferredRenderer::DeleteSwapChain()
    {
        visibilityPipeline0_.reset();

        deferredShadingPipeline_.reset();
        accumulatePipeline_.reset();
        composePipeline_.reset();
        visualDebugPipeline_.reset();
        deferredFrameBuffer0_.reset();
        
        rtPingPong0.reset();
    }

    void HybridDeferredRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = {{0, 0, 0, 0}};
        clearValues[1].depthStencil = {1.0f, 0};
        
        {
            SCOPED_GPU_TIMER("drawpass");

            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            
            renderPassInfo.renderPass = visibilityPipeline0_->RenderPass().Handle();
            renderPassInfo.framebuffer = deferredFrameBuffer0_->Handle();  
            
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = SwapChain().RenderExtent();
            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();

            // make it to generate gbuffer
            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            {
                const auto& scene = GetScene();

                VkDescriptorSet descriptorSets[] = {visibilityPipeline0_->DescriptorSet(imageIndex)};
                VkBuffer vertexBuffers[] = {scene.VertexBuffer().Handle()};
                const VkBuffer indexBuffer = scene.IndexBuffer().Handle();
                VkDeviceSize offsets[] = {0};
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, visibilityPipeline0_->Handle());
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, visibilityPipeline0_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                // indirect draw
                vkCmdDrawIndexedIndirect(commandBuffer, scene.IndirectDrawBuffer().Handle(), 0, scene.GetIndirectDrawBatchCount(), sizeof(VkDrawIndexedIndirectCommand));
            }
            vkCmdEndRenderPass(commandBuffer);
        }

        {
            baseRender_.rtVisibility->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.InitializeBarriers(commandBuffer);
            rtPingPong0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            SCOPED_GPU_TIMER("shadingpass");
            // cs shading pass
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
            
            // bind the global bindless set
            deferredShadingPipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
            
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.rtAccumlation->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        
        {
            SCOPED_GPU_TIMER("reproject pass");
            VkDescriptorSet DescriptorSets[] = {accumulatePipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline_->Handle());
            accumulatePipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.rtOutput->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }
                
        {
            SCOPED_GPU_TIMER("compose pass");
            
            ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[imageIndex], 0,
                           VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_GENERAL);

            baseRender_.rtOutput->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL );
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipeline_->Handle());
            composePipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);

            glm::uvec2 pushConst = GOption->ReferenceMode ? glm::uvec2(0, SwapChain().Extent().height / 2  ) : glm::uvec2(0,0);
            vkCmdPushConstants(commandBuffer, composePipeline_->PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
                               0, sizeof(glm::uvec2), &pushConst);
            
            vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);

            ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[imageIndex], VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }
        
        if (VisualDebug())
        {
            ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[imageIndex], VK_ACCESS_TRANSFER_WRITE_BIT,
                                       VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                       VK_IMAGE_LAYOUT_GENERAL);

            baseRender_.rtMotionVector_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);
            baseRender_.rtOutput->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_GENERAL);

            VkDescriptorSet DescriptorSets[] = {visualDebugPipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, visualDebugPipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    visualDebugPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);
        
    
            ImageMemoryBarrier::FullInsert(commandBuffer, SwapChain().Images()[imageIndex], VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        }

        {
            baseRender_.rtOutput->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
            rtPingPong0->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        
            VkImageCopy copyRegion;
            copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.srcOffset = {0, 0, 0};
            copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copyRegion.dstOffset = {0, 0, 0};
            copyRegion.extent = {rtPingPong0->GetImage().Extent().width, rtPingPong0->GetImage().Extent().height, 1};
        
            vkCmdCopyImage(commandBuffer, baseRender_.rtOutput->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtPingPong0->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
        }
    }
}
