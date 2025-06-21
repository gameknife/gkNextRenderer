#include "HardwareTracingRenderer.hpp"
#include "HardwareTracingPipeline.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"
#include "Assets/Scene.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Math.hpp"
#include "Vulkan/RenderImage.hpp"

#include <array>

namespace Vulkan::HybridDeferred
{
    HardwareTracingRenderer::HardwareTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
    {
    }

    HardwareTracingRenderer::~HardwareTracingRenderer()
    {
    }

    void HardwareTracingRenderer::CreateSwapChain(const VkExtent2D& extent)
    {
        rtPingPong0.reset(new RenderImage(Device(), extent,
                                          VK_FORMAT_R16G16B16A16_SFLOAT,
                                          VK_IMAGE_TILING_OPTIMAL,
                                          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false,"prevoutput"));

        deferredShadingPipeline_.reset(new HardwareTracingPipeline(SwapChain(), GetBaseRender<RayTracing::RayTraceBaseRenderer>().TLAS()[0],
                                                         baseRender_,UniformBuffers(), GetScene()));
        
        accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),baseRender_,rtPingPong0->GetImageView(),UniformBuffers(), GetScene()));
        composePipeline_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), baseRender_, UniformBuffers()));
    }

    void HardwareTracingRenderer::DeleteSwapChain()
    {
        deferredShadingPipeline_.reset();
        accumulatePipeline_.reset();
        composePipeline_.reset();
        rtPingPong0.reset();
    }

    void HardwareTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        baseRender_.InitializeBarriers(commandBuffer);
        rtPingPong0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
        
        {
            SCOPED_GPU_TIMER("shading pass");

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
            deferredShadingPipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
            
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
                          Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.rtAccumlation->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }
        
        {
            SCOPED_GPU_TIMER("reproject pass");

            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline_->Handle());
            accumulatePipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
            vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

            baseRender_.rtOutput->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        }
                
        {
            SCOPED_GPU_TIMER("compose pass");

            SwapChain().InsertBarrierToWrite(commandBuffer, imageIndex);
            
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipeline_->Handle());
            composePipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
            vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);

            SwapChain().InsertBarrierToPresent(commandBuffer, imageIndex);
        }

        {
            SCOPED_GPU_TIMER("copy pass");
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
