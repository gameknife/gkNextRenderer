#include "ModernDeferredRenderer.hpp"
#include "ModernDeferredPipeline.hpp"

#include "Vulkan/Buffer.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/PipelineCommon/CommonComputePipeline.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Utilities/Exception.hpp"
#include <array>

#include "Utilities/Math.hpp"

namespace Vulkan::ModernDeferred {

ModernDeferredRenderer::ModernDeferredRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
	
}

ModernDeferredRenderer::~ModernDeferredRenderer()
{
	ModernDeferredRenderer::DeleteSwapChain();
}

void ModernDeferredRenderer::CreateSwapChain(const VkExtent2D& extent)
{
	const auto format = SwapChain().Format();

	visibilityPipeline_.reset(new VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));

	rtPingPong0.reset(new RenderImage(Device(), extent,
									  VK_FORMAT_R16G16B16A16_SFLOAT,
									  VK_IMAGE_TILING_OPTIMAL,
									  VK_IMAGE_USAGE_STORAGE_BIT));
     
	deferredFrameBuffer_.reset(new FrameBuffer(extent, baseRender_.rtVisibility0->GetImageView(),
		visibilityPipeline_->RenderPass()));
	
	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(),
		baseRender_.rtVisibility0->GetImageView(),
		baseRender_.rtAccumlation->GetImageView(),
		baseRender_.rtMotionVector_->GetImageView(),
		baseRender_.rtAlbedo_->GetImageView(),
		baseRender_.rtNormal_->GetImageView(),
		UniformBuffers(), GetScene()));

	accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),
																		 baseRender_.rtAccumlation->GetImageView(),
																		 rtPingPong0->GetImageView(),
																		 baseRender_.rtMotionVector_->GetImageView(),
																		 baseRender_.rtVisibility0->GetImageView(),
																		 baseRender_.rtVisibility1->GetImageView(),
																		 baseRender_.rtOutput->GetImageView(),
																		 baseRender_.rtNormal_->GetImageView(),
																		 UniformBuffers(), GetScene()));

	composePipeline_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(),
		baseRender_.rtOutput->GetImageView(),
		baseRender_.rtAlbedo_->GetImageView(),
		baseRender_.rtNormal_->GetImageView(),
		baseRender_.rtVisibility0->GetImageView(),
		baseRender_.rtVisibility1->GetImageView(),
		UniformBuffers()));
}

void ModernDeferredRenderer::DeleteSwapChain()
{
	visibilityPipeline_.reset();
	deferredShadingPipeline_.reset();
	
	deferredFrameBuffer_.reset();
	accumulatePipeline_.reset();
	composePipeline_.reset();
	
	rtPingPong0.reset();	
}

void ModernDeferredRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount = 1;

	baseRender_.rtVisibility0->InsertBarrier(commandBuffer, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			   
	std::array<VkClearValue, 2> clearValues = {};
	clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = visibilityPipeline_->RenderPass().Handle();
	renderPassInfo.framebuffer = deferredFrameBuffer_->Handle();
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = SwapChain().RenderExtent();
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();

	
	{
		SCOPED_GPU_TIMER("drawpass");
		// make it to generate gbuffer
		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		{
			const auto& scene = GetScene();

			VkDescriptorSet descriptorSets[] = { visibilityPipeline_->DescriptorSet(imageIndex) };
			VkBuffer vertexBuffers[] = { scene.VertexBuffer().Handle() };
			const VkBuffer indexBuffer = scene.IndexBuffer().Handle();
			VkDeviceSize offsets[] = { 0 };

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, visibilityPipeline_->Handle());
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, visibilityPipeline_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			// indirect draw
			vkCmdDrawIndexedIndirect(commandBuffer, scene.IndirectDrawBuffer().Handle(), 0, scene.GetIndirectDrawBatchCount(), sizeof(VkDrawIndexedIndirectCommand));
		}
		vkCmdEndRenderPass(commandBuffer);

		// always draw to visibility0, then copy to visibility1 after the shading, v0 is current frame, v1 is last frame
	}

	{
		baseRender_.rtOutput->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.rtAccumlation->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		rtPingPong0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.rtVisibility0->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.rtVisibility1->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.rtMotionVector_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.rtAlbedo_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.rtNormal_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
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
		

		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 4, 1);

		baseRender_.rtAccumlation->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}

	{
		SCOPED_GPU_TIMER("reproject pass");
		VkDescriptorSet DescriptorSets[] = {accumulatePipeline_->DescriptorSet(imageIndex)};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline_->Handle());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
								accumulatePipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
		vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 4), 1);

		baseRender_.rtOutput->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
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
		
		glm::uvec2 pushConst = GOption->ReferenceMode ? glm::uvec2(SwapChain().Extent().width / 2, 0  ) : glm::uvec2(0,0);
		vkCmdPushConstants(commandBuffer, composePipeline_->PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
						   0, sizeof(glm::uvec2), &pushConst);
		
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);

		ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
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
