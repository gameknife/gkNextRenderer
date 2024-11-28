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

namespace Vulkan::ModernDeferred {

ModernDeferredRenderer::ModernDeferredRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
	
}

ModernDeferredRenderer::~ModernDeferredRenderer()
{
	ModernDeferredRenderer::DeleteSwapChain();
}

void ModernDeferredRenderer::CreateSwapChain()
{
	const auto extent = SwapChain().Extent();
	const auto format = SwapChain().Format();

	visibilityPipeline_.reset(new VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));
	
	rtVisibilityBuffer_.reset(new RenderImage(Device(), extent, VK_FORMAT_R32G32_UINT, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));

	rtOutput_.reset(new RenderImage(Device(), extent, format, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));

	rtMotionVector_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT));
	
	
	deferredFrameBuffer_.reset(new FrameBuffer(rtVisibilityBuffer_->GetImageView(), visibilityPipeline_->RenderPass()));
	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(), rtVisibilityBuffer_->GetImageView(), rtOutput_->GetImageView(), rtMotionVector_->GetImageView(), UniformBuffers(), GetScene()));

}

void ModernDeferredRenderer::DeleteSwapChain()
{
	visibilityPipeline_.reset();
	deferredShadingPipeline_.reset();
	
	deferredFrameBuffer_.reset();

	rtVisibilityBuffer_.reset();
	rtOutput_.reset();
	rtMotionVector_.reset();
}

void ModernDeferredRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount = 1;

	rtVisibilityBuffer_->InsertBarrier(commandBuffer, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
			   
	std::array<VkClearValue, 2> clearValues = {};
	clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
	clearValues[1].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = visibilityPipeline_->RenderPass().Handle();
	renderPassInfo.framebuffer = deferredFrameBuffer_->Handle();
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = SwapChain().Extent();
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

		rtOutput_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL);

		rtMotionVector_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
			VK_IMAGE_LAYOUT_GENERAL);

		rtVisibilityBuffer_->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
			VK_IMAGE_LAYOUT_GENERAL);
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
		
#if ANDROID
		vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 32, SwapChain().Extent().height / 32, 1);	
#else
		vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);	
#endif

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
	copyRegion.extent = {SwapChain().Extent().width, SwapChain().Extent().height, 1};

	vkCmdCopyImage(commandBuffer,
				   rtOutput_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				   SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				   1, &copyRegion);

	ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
							   VK_ACCESS_TRANSFER_WRITE_BIT,
							   0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
}
