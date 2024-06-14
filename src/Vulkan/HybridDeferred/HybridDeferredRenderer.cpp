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

#include "Vulkan/ModernDeferred/ModernDeferredPipeline.hpp"

namespace Vulkan::HybridDeferred
{
    HybridDeferredRenderer::HybridDeferredRenderer(const WindowConfig& windowConfig, const VkPresentModeKHR presentMode, const bool enableValidationLayers) :
        RayTracing::RayTraceBaseRenderer(windowConfig, presentMode, enableValidationLayers)
    {
    }

    HybridDeferredRenderer::~HybridDeferredRenderer()
    {
        HybridDeferredRenderer::DeleteSwapChain();
    }

    void HybridDeferredRenderer::CreateSwapChain()
    {
        RayTracing::RayTraceBaseRenderer::CreateSwapChain();

        const auto extent = SwapChain().Extent();
        const auto format = SwapChain().Format();

        visibilityPipeline_.reset(new Vulkan::ModernDeferred::VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));

        visibilityBufferImage_.reset(new Image(Device(), extent,
                                               VK_FORMAT_R32G32_UINT, VK_IMAGE_TILING_OPTIMAL,
                                               VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
        visibilityBufferImageMemory_.reset(
            new DeviceMemory(visibilityBufferImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
        visibilityBufferImageView_.reset(new ImageView(Device(), visibilityBufferImage_->Handle(),
                                                       VK_FORMAT_R32G32_UINT,
                                                       VK_IMAGE_ASPECT_COLOR_BIT));

        visibility1BufferImage_.reset(new Image(Device(), extent,
                                                VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL,
                                                VK_IMAGE_USAGE_STORAGE_BIT));
        visibility1BufferImageMemory_.reset(
            new DeviceMemory(visibility1BufferImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
        visibility1BufferImageView_.reset(new ImageView(Device(), visibility1BufferImage_->Handle(),
                                                        VK_FORMAT_R32_UINT,
                                                        VK_IMAGE_ASPECT_COLOR_BIT));

        outputImage_.reset(new Image(Device(), extent, format,
                                     VK_IMAGE_TILING_OPTIMAL,
                                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
        outputImageMemory_.reset(
            new DeviceMemory(outputImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
        outputImageView_.reset(new ImageView(Device(), outputImage_->Handle(),
                                             format,
                                             VK_IMAGE_ASPECT_COLOR_BIT));

        motionVectorImage_.reset(new Image(Device(), extent, VK_FORMAT_R32G32_SFLOAT,
                                           VK_IMAGE_TILING_OPTIMAL,
                                           VK_IMAGE_USAGE_STORAGE_BIT));
        motionVectorImageMemory_.reset(
            new DeviceMemory(motionVectorImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
        motionVectorImageView_.reset(new ImageView(Device(), motionVectorImage_->Handle(),
                                                   VK_FORMAT_R32G32_SFLOAT,
                                                   VK_IMAGE_ASPECT_COLOR_BIT));
        
        accumulationImage_.reset(new Image(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                                 VK_IMAGE_USAGE_STORAGE_BIT));
        accumulationImageMemory_.reset(
            new DeviceMemory(accumulationImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
        accumulationImageView_.reset(new ImageView(Device(), accumulationImage_->Handle(),
                                                   VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT));
        
        pingpongImage0_.reset(new Image(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                      VK_IMAGE_USAGE_STORAGE_BIT));
        pingpongImage0Memory_.reset(
            new DeviceMemory(pingpongImage0_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
        pingpongImage0View_.reset(new ImageView(Device(), pingpongImage0_->Handle(), VK_FORMAT_R16G16B16A16_SFLOAT,
                                                VK_IMAGE_ASPECT_COLOR_BIT));

        pingpongImage1_.reset(new Image(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
                                        VK_IMAGE_USAGE_STORAGE_BIT));
        pingpongImage1Memory_.reset(
            new DeviceMemory(pingpongImage1_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
        pingpongImage1View_.reset(new ImageView(Device(), pingpongImage1_->Handle(), VK_FORMAT_R16G16B16A16_SFLOAT,
                                                VK_IMAGE_ASPECT_COLOR_BIT));

        deferredFrameBuffer_.reset(new FrameBuffer(*visibilityBufferImageView_, visibilityPipeline_->RenderPass()));
        deferredShadingPipeline_.reset(new HybridShadingPipeline(SwapChain(), topAs_[0], *visibilityBufferImageView_, *accumulationImageView_, *motionVectorImageView_, UniformBuffers(), GetScene()));
        accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),
                    *accumulationImageView_,
                    *pingpongImage0View_,
                    *pingpongImage1View_,
                    *motionVectorImageView_,
                    *visibilityBufferImageView_,
                    *visibility1BufferImageView_,
                    *outputImageView_,
                    UniformBuffers(), GetScene()));
        
        const auto& debugUtils = Device().DebugUtils();
        debugUtils.SetObjectName(outputImage_->Handle(), "Output Image");
        debugUtils.SetObjectName(visibilityBufferImage_->Handle(), "Visibility Image");
    }

    void HybridDeferredRenderer::DeleteSwapChain()
    {
        visibilityPipeline_.reset();
        deferredShadingPipeline_.reset();
        accumulatePipeline_.reset();

        deferredFrameBuffer_.reset();

        visibilityBufferImage_.reset();
        visibilityBufferImageMemory_.reset();
        visibilityBufferImageView_.reset();

        outputImage_.reset();
        outputImageMemory_.reset();
        outputImageView_.reset();

        motionVectorImage_.reset();
        motionVectorImageMemory_.reset();
        motionVectorImageView_.reset();

        accumulationImage_.reset();
        accumulationImageMemory_.reset();
        accumulationImageView_.reset();

        pingpongImage0_.reset();
        pingpongImage0Memory_.reset();
        pingpongImage0View_.reset();

        pingpongImage1_.reset();
        pingpongImage1Memory_.reset();
        pingpongImage1View_.reset();

        visibility1BufferImage_.reset();
        visibility1BufferImageMemory_.reset();
        visibility1BufferImageView_.reset();
        
        RayTracing::RayTraceBaseRenderer::DeleteSwapChain();
    }

    void HybridDeferredRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        VkImageSubresourceRange subresourceRange = {};
        subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        subresourceRange.baseMipLevel = 0;
        subresourceRange.levelCount = 1;
        subresourceRange.baseArrayLayer = 0;
        subresourceRange.layerCount = 1;

        ImageMemoryBarrier::Insert(commandBuffer, visibilityBufferImage_->Handle(), subresourceRange,
                                   0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        std::array<VkClearValue, 2> clearValues = {};
        clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = visibilityPipeline_->RenderPass().Handle();
        renderPassInfo.framebuffer = deferredFrameBuffer_->Handle();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = SwapChain().Extent();
        renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
        renderPassInfo.pClearValues = clearValues.data();

        {
            SCOPED_GPU_TIMER("drawpass");
            // make it to generate gbuffer
            vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
            {
                const auto& scene = GetScene();

                VkDescriptorSet descriptorSets[] = {visibilityPipeline_->DescriptorSet(imageIndex)};
                VkBuffer vertexBuffers[] = {scene.VertexBuffer().Handle()};
                const VkBuffer indexBuffer = scene.IndexBuffer().Handle();
                VkDeviceSize offsets[] = {0};

                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, visibilityPipeline_->Handle());
                vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, visibilityPipeline_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);
                vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
                vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                // indirect draw
                vkCmdDrawIndexedIndirect(commandBuffer, scene.IndirectDrawBuffer().Handle(), 0, scene.GetIndirectDrawBatchCount(), sizeof(VkDrawIndexedIndirectCommand));
            }
            vkCmdEndRenderPass(commandBuffer);

            ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange,
                                       0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, motionVectorImage_->Handle(), subresourceRange,
                                       0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, visibilityBufferImage_->Handle(), subresourceRange,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                       VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, visibility1BufferImage_->Handle(), subresourceRange,
                           0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, accumulationImage_->Handle(), subresourceRange,
               0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
               VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, pingpongImage0_->Handle(), subresourceRange,
   0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
   VK_IMAGE_LAYOUT_GENERAL);
            ImageMemoryBarrier::Insert(commandBuffer, pingpongImage1_->Handle(), subresourceRange,
0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            SCOPED_GPU_TIMER("shadingpass");
            // cs shading pass
            VkDescriptorSet DescriptorSets[] = {deferredShadingPipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    deferredShadingPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
#if ANDROID
		vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 32 / ( CheckerboxRendering() ? 2 : 1 ), SwapChain().Extent().height / 32, 1);	
#else
            vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8 / (CheckerboxRendering() ? 2 : 1), SwapChain().Extent().height / 4, 1);
#endif

            ImageMemoryBarrier::Insert(commandBuffer, accumulationImage_->Handle(), subresourceRange,
   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
   VK_IMAGE_LAYOUT_GENERAL);
        }

        {
            SCOPED_GPU_TIMER("reproject pass");
            VkDescriptorSet DescriptorSets[] = {accumulatePipeline_->DescriptorSet(imageIndex)};
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline_->Handle());
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                    accumulatePipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
            vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);

            ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange,
                           VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

            ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
                                       VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        // Copy output image into swap-chain image.
        VkImageCopy copyRegion;
        copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.srcOffset = {0, 0, 0};
        copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copyRegion.dstOffset = {0, 0, 0};
        copyRegion.extent = {SwapChain().Extent().width, SwapChain().Extent().height, 1};

        vkCmdCopyImage(commandBuffer,
                       outputImage_->Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &copyRegion);

        ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
                                   VK_ACCESS_TRANSFER_WRITE_BIT,
                                   0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    }
}
