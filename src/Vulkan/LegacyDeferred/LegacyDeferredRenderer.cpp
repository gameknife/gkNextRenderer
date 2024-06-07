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
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Utilities/Exception.hpp"
#include <array>

namespace Vulkan::LegacyDeferred {

LegacyDeferredRenderer::LegacyDeferredRenderer(const WindowConfig& windowConfig, const VkPresentModeKHR presentMode, const bool enableValidationLayers) :
	Vulkan::VulkanBaseRenderer(windowConfig, presentMode, enableValidationLayers)
{
	
}

LegacyDeferredRenderer::~LegacyDeferredRenderer()
{
	LegacyDeferredRenderer::DeleteSwapChain();
}
	
void LegacyDeferredRenderer::CreateSwapChain()
{
	Vulkan::VulkanBaseRenderer::CreateSwapChain();
	
	const auto extent = SwapChain().Extent();
	const auto format = SwapChain().Format();

	gbufferPipeline_.reset(new GBufferPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));

	gbuffer0BufferImage_.reset(new Image(Device(), extent,
	VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
	VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
	gbufferBuffer0ImageMemory_.reset(
		new DeviceMemory(gbuffer0BufferImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	gbufferBuffer0ImageView_.reset(new ImageView(Device(), gbuffer0BufferImage_->Handle(),
		VK_FORMAT_B8G8R8A8_UNORM,
		VK_IMAGE_ASPECT_COLOR_BIT));
	
	gbuffer1BufferImage_.reset(new Image(Device(), extent,
		VK_FORMAT_R16G16B16A16_SFLOAT, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
	gbufferBuffer1ImageMemory_.reset(
		new DeviceMemory(gbuffer1BufferImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	gbufferBuffer1ImageView_.reset(new ImageView(Device(), gbuffer1BufferImage_->Handle(),
		VK_FORMAT_R16G16B16A16_SFLOAT,
		VK_IMAGE_ASPECT_COLOR_BIT));

	gbuffer2BufferImage_.reset(new Image(Device(), extent,
	VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TILING_OPTIMAL,
	VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
	gbufferBuffer2ImageMemory_.reset(
		new DeviceMemory(gbuffer2BufferImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	gbufferBuffer2ImageView_.reset(new ImageView(Device(), gbuffer2BufferImage_->Handle(),
		VK_FORMAT_B8G8R8A8_UNORM,
		VK_IMAGE_ASPECT_COLOR_BIT));
	
	outputImage_.reset(new Image(Device(), extent, format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
	outputImageMemory_.reset(
		new DeviceMemory(outputImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	outputImageView_.reset(new ImageView(Device(), outputImage_->Handle(),
		format,
		VK_IMAGE_ASPECT_COLOR_BIT));

	// MRT
	deferredFrameBuffer_.reset(new FrameBuffer(*gbufferBuffer0ImageView_, *gbufferBuffer1ImageView_, *gbufferBuffer2ImageView_, gbufferPipeline_->RenderPass()));
	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(), *gbufferBuffer0ImageView_,
		*gbufferBuffer1ImageView_,
		*gbufferBuffer2ImageView_,
		*outputImageView_, UniformBuffers(), GetScene()));

	const auto& debugUtils = Device().DebugUtils();
	debugUtils.SetObjectName(outputImage_->Handle(), "Output Image");
	
	debugUtils.SetObjectName(gbuffer0BufferImage_->Handle(), "GBuffer0 Image");
	debugUtils.SetObjectName(gbuffer1BufferImage_->Handle(), "GBuffer1 Image");
	debugUtils.SetObjectName(gbuffer2BufferImage_->Handle(), "GBuffer2 Image");

	debugUtils.SetObjectName(gbufferBuffer0ImageView_->Handle(), "GBuffer0 Image View");
	debugUtils.SetObjectName(gbufferBuffer1ImageView_->Handle(), "GBuffer1 Image View");
	debugUtils.SetObjectName(gbufferBuffer2ImageView_->Handle(), "GBuffer2 Image View");
}

void LegacyDeferredRenderer::DeleteSwapChain()
{
	gbufferPipeline_.reset();
	deferredShadingPipeline_.reset();
	deferredFrameBuffer_.reset();

	gbuffer0BufferImage_.reset();
	gbufferBuffer0ImageMemory_.reset();
	gbufferBuffer0ImageView_.reset();
	
	gbuffer1BufferImage_.reset();
	gbufferBuffer1ImageMemory_.reset();
	gbufferBuffer1ImageView_.reset();
	
	gbuffer2BufferImage_.reset();
	gbufferBuffer2ImageMemory_.reset();
	gbufferBuffer2ImageView_.reset();

	outputImage_.reset();
	outputImageMemory_.reset();
	outputImageView_.reset();

	Vulkan::VulkanBaseRenderer::DeleteSwapChain();
}

void LegacyDeferredRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
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

	// make it to generate gbuffer
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

		// traditional drawcall
		
		// uint32_t vertexOffset = 0;
		// uint32_t indexOffset = 0;
		// uint32_t instanceOffset = 0;
		
		// for (uint32_t m = 0; m < scene.Models().size(); m++)
		// {
		// 	const auto& model = scene.Models()[m];
		// 	const auto vertexCount = static_cast<uint32_t>(model.NumberOfVertices());
		// 	const auto indexCount = static_cast<uint32_t>(model.NumberOfIndices());
		//
		// 	// with node as instance, offset to its idx, matrix from matrxibuffer
		// 	uint32_t instanceCount = scene.ModelInstanceCount()[m];
		// 	vkCmdDrawIndexed(commandBuffer, indexCount, instanceCount, indexOffset, vertexOffset, instanceOffset);
		//
		// 	vertexOffset += vertexCount;
		// 	indexOffset += indexCount;
		// 	instanceOffset += instanceCount;
		// }

		// indirect draw
		vkCmdDrawIndexedIndirect(commandBuffer, scene.IndirectDrawBuffer().Handle(), 0, scene.Nodes().size(), sizeof(VkDrawIndexedIndirectCommand));
	}
	vkCmdEndRenderPass(commandBuffer);

	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount = 1;

	ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange,
					   0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					   VK_IMAGE_LAYOUT_GENERAL);
	ImageMemoryBarrier::Insert(commandBuffer, gbuffer0BufferImage_->Handle(), subresourceRange,
					   0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					   VK_IMAGE_LAYOUT_GENERAL);
	ImageMemoryBarrier::Insert(commandBuffer, gbuffer1BufferImage_->Handle(), subresourceRange,
				   0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				   VK_IMAGE_LAYOUT_GENERAL);
	ImageMemoryBarrier::Insert(commandBuffer, gbuffer2BufferImage_->Handle(), subresourceRange,
				   0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				   VK_IMAGE_LAYOUT_GENERAL);
	// cs shading pass
	VkDescriptorSet denoiserDescriptorSets[] = {deferredShadingPipeline_->DescriptorSet(imageIndex)};
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
							deferredShadingPipeline_->PipelineLayout().Handle(), 0, 1, denoiserDescriptorSets, 0, nullptr);
	vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8 / ( CheckerboxRendering() ? 2 : 1 ), SwapChain().Extent().height / 4, 1);
	
	// copy to swap-buffer
	ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange,
						   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
						   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

	ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange, 0,
							   VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
							   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

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
