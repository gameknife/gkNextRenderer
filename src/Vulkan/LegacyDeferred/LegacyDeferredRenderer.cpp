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

LegacyDeferredRenderer::LegacyDeferredRenderer(RayTracing::RayTraceBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
	
}

LegacyDeferredRenderer::~LegacyDeferredRenderer()
{
	LegacyDeferredRenderer::DeleteSwapChain();
}
	
void LegacyDeferredRenderer::CreateSwapChain()
{
	const auto extent = SwapChain().Extent();
	const auto format = SwapChain().Format();

	gbufferPipeline_.reset(new GBufferPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));

	rtGBuffer0_.reset(new RenderImage(Device(), extent, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));

	rtGBuffer1_.reset(new RenderImage(Device(), extent, VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));

	rtGBuffer2_.reset(new RenderImage(Device(), extent, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));

	rtOutput_.reset(new RenderImage(Device(), extent, format, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));

	// MRT
	deferredFrameBuffer_.reset(new FrameBuffer(rtGBuffer0_->GetImageView(), rtGBuffer1_->GetImageView(), rtGBuffer2_->GetImageView(), gbufferPipeline_->RenderPass()));
	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(), rtGBuffer0_->GetImageView(), rtGBuffer1_->GetImageView(), rtGBuffer2_->GetImageView(), rtOutput_->GetImageView(), UniformBuffers(), GetScene()));
}

void LegacyDeferredRenderer::DeleteSwapChain()
{
	gbufferPipeline_.reset();
	deferredShadingPipeline_.reset();
	deferredFrameBuffer_.reset();

	rtGBuffer0_.reset();
	rtGBuffer1_.reset();
	rtGBuffer2_.reset();
	rtOutput_.reset();
}

void LegacyDeferredRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount = 1;

	rtGBuffer0_->InsertBarrier(commandBuffer, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
							   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	rtGBuffer1_->InsertBarrier(commandBuffer, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
							   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
	rtGBuffer2_->InsertBarrier(commandBuffer, 0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
							   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);	

	std::array<VkClearValue, 4> clearValues = {};
	clearValues[0].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
	clearValues[1].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
	clearValues[2].color = { {0.0f, 0.0f, 0.0f, 1.0f} };
	clearValues[3].depthStencil = { 1.0f, 0 };

	VkRenderPassBeginInfo renderPassInfo = {};
	renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	renderPassInfo.renderPass = gbufferPipeline_->RenderPass().Handle();
	renderPassInfo.framebuffer = deferredFrameBuffer_->Handle();
	renderPassInfo.renderArea.offset = { 0, 0 };
	renderPassInfo.renderArea.extent = SwapChain().Extent();
	renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
	renderPassInfo.pClearValues = clearValues.data();
	
	{
		SCOPED_GPU_TIMER("drawpass");
		vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		{
			const auto& scene = GetScene();

			VkDescriptorSet descriptorSets[] = { gbufferPipeline_->DescriptorSet(imageIndex) };
			VkBuffer vertexBuffers[] = { scene.VertexBuffer().Handle() };
			const VkBuffer indexBuffer = scene.IndexBuffer().Handle();
			VkDeviceSize offsets[] = { 0 };

			vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline_->Handle());
			vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline_->PipelineLayout().Handle(), 0, 1, descriptorSets, 0, nullptr);
			vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
			vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			// bind the global bindless set
			static const uint32_t k_bindless_set = 1;
			VkDescriptorSet GlobalDescriptorSets[] = { Assets::GlobalTexturePool::GetInstance()->DescriptorSet(0) };
			vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, gbufferPipeline_->PipelineLayout().Handle(), k_bindless_set,
									 1, GlobalDescriptorSets, 0, nullptr );

			// indirect draw
			vkCmdDrawIndexedIndirect(commandBuffer, scene.IndirectDrawBuffer().Handle(), 0, scene.GetIndirectDrawBatchCount(), sizeof(VkDrawIndexedIndirectCommand));
		}
		vkCmdEndRenderPass(commandBuffer);

		rtOutput_->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
								 VK_IMAGE_LAYOUT_GENERAL);
		rtGBuffer0_->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, 
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);

		rtGBuffer1_->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);

		rtGBuffer2_->InsertBarrier(commandBuffer, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
		VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_GENERAL);
	}
	

	{
		SCOPED_GPU_TIMER("shadingpass");
		
		// cs shading pass
		VkDescriptorSet denoiserDescriptorSets[] = {deferredShadingPipeline_->DescriptorSet(imageIndex)};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
								deferredShadingPipeline_->PipelineLayout().Handle(), 0, 1, denoiserDescriptorSets, 0, nullptr);
#if ANDROID
		vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 32, SwapChain().Extent().height / 32, 1);	
#else
		vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);	
#endif
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
