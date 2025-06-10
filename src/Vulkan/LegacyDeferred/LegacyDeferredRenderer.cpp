#include "LegacyDeferredRenderer.hpp"
#include "LegacyDeferredPipeline.hpp"

#include "Vulkan/Buffer.hpp"
#include "Vulkan/Device.hpp"
#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/ImageMemoryBarrier.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Utilities/Exception.hpp"
#include <array>

namespace Vulkan::LegacyDeferred {

LegacyDeferredRenderer::LegacyDeferredRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
	
}

LegacyDeferredRenderer::~LegacyDeferredRenderer()
{
	DeleteSwapChain();
}
	
void LegacyDeferredRenderer::CreateSwapChain(const VkExtent2D& extent)
{
	const auto format = SwapChain().Format();

	visibilityPipeline0_.reset(new Vulkan::ModernDeferred::VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));
	deferredFrameBuffer_.reset(new FrameBuffer(extent, baseRender_.rtVisibility0->GetImageView(), visibilityPipeline0_->RenderPass()));
	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(), baseRender_.rtVisibility0->GetImageView(), baseRender_.rtOutput->GetImageView(), UniformBuffers(), GetScene()));
	composePipeline_.reset(new Vulkan::PipelineCommon::SimpleComposePipeline(SwapChain(), baseRender_.rtOutput->GetImageView(), UniformBuffers()));
}
	
void LegacyDeferredRenderer::DeleteSwapChain()
{
	composePipeline_.reset();
	visibilityPipeline0_.reset();
	deferredShadingPipeline_.reset();
	deferredFrameBuffer_.reset();
}

void LegacyDeferredRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
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
	renderPassInfo.renderPass = visibilityPipeline0_->RenderPass().Handle();
	renderPassInfo.framebuffer = deferredFrameBuffer_->Handle();
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = SwapChain().RenderExtent();
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();
	
	{
		SCOPED_GPU_TIMER("drawpass");
		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		{
			const auto& scene = GetScene();

			VkDescriptorSet descriptorSets[] = { visibilityPipeline0_->DescriptorSet(imageIndex) };
			VkBuffer vertexBuffers[] = { scene.VertexBuffer().Handle() };
			const VkBuffer indexBuffer = scene.IndexBuffer().Handle();
			VkDeviceSize offsets[] = { 0 };

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, visibilityPipeline0_->Handle());
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, visibilityPipeline0_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
			
			// indirect draw
			vkCmdDrawIndexedIndirect(commandBuffer, scene.IndirectDrawBuffer().Handle(), 0, scene.GetIndirectDrawBatchCount(), sizeof(VkDrawIndexedIndirectCommand));
		}
		vkCmdEndRenderPass(commandBuffer);

		baseRender_.rtOutput->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.rtVisibility0->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);
	}
	
	{
		SCOPED_GPU_TIMER("shadingpass");
		
		// cs shading pass
		VkDescriptorSet denoiserDescriptorSets[] = {deferredShadingPipeline_->DescriptorSet(imageIndex)};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
								deferredShadingPipeline_->PipelineLayout().Handle(), 0, 1, denoiserDescriptorSets, 0, nullptr);

		// bind the global bindless set
		static const uint32_t k_bindless_set = 1;
		VkDescriptorSet GlobalDescriptorSets[] = { Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0) };
		vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->PipelineLayout().Handle(), k_bindless_set,
								 1, GlobalDescriptorSets, 0, nullptr );
		
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);	

		// copy to swap-buffer
		baseRender_.rtOutput->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_GENERAL);
	}

	{
		ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
					   VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					   VK_IMAGE_LAYOUT_GENERAL);
            
		VkDescriptorSet DescriptorSets[] = {composePipeline_->DescriptorSet(imageIndex)};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, composePipeline_->Handle());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
								composePipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
		
		glm::uvec2 pushConst = GOption->ReferenceMode ? glm::uvec2(0, 0  ) : glm::uvec2(0,0);
		vkCmdPushConstants(commandBuffer, composePipeline_->PipelineLayout().Handle(), VK_SHADER_STAGE_COMPUTE_BIT,
						   0, sizeof(glm::uvec2), &pushConst);
		
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);

		ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
	}
}
}

Vulkan::VoxelTracing::VoxelTracingRenderer::VoxelTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
}

Vulkan::VoxelTracing::VoxelTracingRenderer::~VoxelTracingRenderer()
{
	DeleteSwapChain();
}

void Vulkan::VoxelTracing::VoxelTracingRenderer::CreateSwapChain(const VkExtent2D& extent)
{
	const auto format = SwapChain().Format();
	
	rtOutput_.reset(new RenderImage(Device(), extent, format, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
	
	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(), rtOutput_->GetImageView(), UniformBuffers(), GetScene()));
}

void Vulkan::VoxelTracing::VoxelTracingRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
	rtOutput_.reset();
}

void Vulkan::VoxelTracing::VoxelTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount = 1;
	
	{
		SCOPED_GPU_TIMER("shadingpass");
		
		// cs shading pass
		VkDescriptorSet denoiserDescriptorSets[] = {deferredShadingPipeline_->DescriptorSet(imageIndex)};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
								deferredShadingPipeline_->PipelineLayout().Handle(), 0, 1, denoiserDescriptorSets, 0, nullptr);

		// bind the global bindless set
		static const uint32_t k_bindless_set = 1;
		VkDescriptorSet GlobalDescriptorSets[] = { Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0) };
		vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->PipelineLayout().Handle(), k_bindless_set,
								 1, GlobalDescriptorSets, 0, nullptr );
		
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 4, 1);	

		// copy to swap-buffer
		rtOutput_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
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
	copyRegion.extent = {rtOutput_->GetImage().Extent().width, rtOutput_->GetImage().Extent().height, 1};

	vkCmdCopyImage(commandBuffer,
				   rtOutput_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				   SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				   1, &copyRegion);

	ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
							   VK_ACCESS_TRANSFER_WRITE_BIT,
							   0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
