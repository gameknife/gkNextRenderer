#include "Application.hpp"
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
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Assets/UniformBuffer.hpp"
#include "Utilities/Exception.hpp"
#include <array>

namespace Vulkan::ModernDeferred {

ModernDeferredRenderer::ModernDeferredRenderer(const WindowConfig& windowConfig, const VkPresentModeKHR presentMode, const bool enableValidationLayers) :
	Vulkan::VulkanBaseRenderer(windowConfig, presentMode, enableValidationLayers)
{
	
}

ModernDeferredRenderer::~ModernDeferredRenderer()
{
	ModernDeferredRenderer::DeleteSwapChain();
}

void ModernDeferredRenderer::CreateSwapChain()
{
	Vulkan::VulkanBaseRenderer::CreateSwapChain();
	
	const auto extent = SwapChain().Extent();
	const auto format = SwapChain().Format();

	visibilityPipeline_.reset(new VisibilityPipeline(SwapChain(), DepthBuffer(), UniformBuffers(), GetScene()));
	
	visibilityBufferImage_.reset(new Image(Device(), extent,
		VK_FORMAT_R32_UINT, VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT));
	visibilityBufferImageMemory_.reset(
		new DeviceMemory(visibilityBufferImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	visibilityBufferImageView_.reset(new ImageView(Device(), visibilityBufferImage_->Handle(),
		VK_FORMAT_R32_UINT,
		VK_IMAGE_ASPECT_COLOR_BIT));

	outputImage_.reset(new Image(Device(), extent, format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT));
	outputImageMemory_.reset(
		new DeviceMemory(outputImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	outputImageView_.reset(new ImageView(Device(), outputImage_->Handle(),
		format,
		VK_IMAGE_ASPECT_COLOR_BIT));

	accumulateImage_.reset(new Image(Device(), extent, format,
		VK_IMAGE_TILING_OPTIMAL,
		VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
	accumulateImageMemory_.reset(
		new DeviceMemory(accumulateImage_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	accumulateImageView_.reset(new ImageView(Device(), accumulateImage_->Handle(),
		format,
		VK_IMAGE_ASPECT_COLOR_BIT));

	accumulateImage1_.reset(new Image(Device(), extent, format,
	VK_IMAGE_TILING_OPTIMAL,
	VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
	accumulateImage1Memory_.reset(
		new DeviceMemory(accumulateImage1_->AllocateMemory(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)));
	accumulateImage1View_.reset(new ImageView(Device(), accumulateImage1_->Handle(),
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
	
	
	deferredFrameBuffer_.reset(new FrameBuffer(*visibilityBufferImageView_, visibilityPipeline_->RenderPass()));
	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(), *visibilityBufferImageView_, *outputImageView_, *motionVectorImageView_, UniformBuffers(), GetScene()));
	accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(), *outputImageView_, *accumulateImageView_, *accumulateImage1View_, *motionVectorImageView_, UniformBuffers(), GetScene()));

	const auto& debugUtils = Device().DebugUtils();
	debugUtils.SetObjectName(outputImage_->Handle(), "Output Image");
	debugUtils.SetObjectName(visibilityBufferImage_->Handle(), "Visibility Image");
	debugUtils.SetObjectName(accumulateImage_->Handle(), "Accumulate Image");
}

void ModernDeferredRenderer::DeleteSwapChain()
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

	accumulateImage_.reset();
	accumulateImageMemory_.reset();
	accumulateImageView_.reset();

	accumulateImage1_.reset();
	accumulateImage1Memory_.reset();
	accumulateImage1View_.reset();
	
	motionVectorImage_.reset();
	motionVectorImageMemory_.reset();
	motionVectorImageView_.reset();

	Vulkan::VulkanBaseRenderer::DeleteSwapChain();
}

void ModernDeferredRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
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

		uint32_t vertexOffset = 0;
		uint32_t indexOffset = 0;

		// drawcall
		for (const auto& model : scene.Models())
		{
			const auto vertexCount = static_cast<uint32_t>(model.NumberOfVertices());
			const auto indexCount = static_cast<uint32_t>(model.NumberOfIndices());

			vkCmdDrawIndexed(commandBuffer, indexCount, 1, indexOffset, vertexOffset, 0);

			vertexOffset += vertexCount;
			indexOffset += indexCount;
		}
	}
	vkCmdEndRenderPass(commandBuffer);

	VkImageSubresourceRange subresourceRange = {};
	subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	subresourceRange.baseMipLevel = 0;
	subresourceRange.levelCount = 1;
	subresourceRange.baseArrayLayer = 0;
	subresourceRange.layerCount = 1;

	ImageMemoryBarrier::Insert(commandBuffer, accumulateImage_->Handle(), subresourceRange,
					   0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					   VK_IMAGE_LAYOUT_GENERAL);
	ImageMemoryBarrier::Insert(commandBuffer, accumulateImage1_->Handle(), subresourceRange,
				   0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				   VK_IMAGE_LAYOUT_GENERAL);
	ImageMemoryBarrier::Insert(commandBuffer, outputImage_->Handle(), subresourceRange,
				   0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				   VK_IMAGE_LAYOUT_GENERAL);
	ImageMemoryBarrier::Insert(commandBuffer, visibilityBufferImage_->Handle(), subresourceRange,
					   0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
					   VK_IMAGE_LAYOUT_GENERAL);

	ImageMemoryBarrier::Insert(commandBuffer, motionVectorImage_->Handle(), subresourceRange,
				   0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
				   VK_IMAGE_LAYOUT_GENERAL);
	
	// cs shading pass
	{
		VkDescriptorSet DescriptorSets[] = {deferredShadingPipeline_->DescriptorSet(imageIndex)};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
								deferredShadingPipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
		vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8 / ( CheckerboxRendering() ? 2 : 1 ), SwapChain().Extent().height / 4, 1);	
	}

	ImageMemoryBarrier::Insert(commandBuffer, motionVectorImage_->Handle(), subresourceRange,
			   VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL,
			   VK_IMAGE_LAYOUT_GENERAL);

	// cs shading pass
	// ping pong
	{
		VkDescriptorSet DescriptorSets[] = {accumulatePipeline_->DescriptorSet(imageIndex)};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline_->Handle());
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
								accumulatePipeline_->PipelineLayout().Handle(), 0, 1, DescriptorSets, 0, nullptr);
		vkCmdDispatch(commandBuffer, SwapChain().Extent().width / 8, SwapChain().Extent().height / 4, 1);
	}
	
	// copy to swap-buffer
	VkImage srcAccumulateImage = frameCount_ % 2 == 0 ? accumulateImage1_->Handle() : accumulateImage_->Handle();
	
	ImageMemoryBarrier::Insert(commandBuffer, srcAccumulateImage, subresourceRange,
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
				   srcAccumulateImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
				   SwapChain().Images()[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
				   1, &copyRegion);

	ImageMemoryBarrier::Insert(commandBuffer, SwapChain().Images()[imageIndex], subresourceRange,
							   VK_ACCESS_TRANSFER_WRITE_BIT,
							   0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}
}
