#include "SoftwareTracingRenderer.hpp"
#include "SoftwareTracingPipeline.hpp"

#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/SwapChain.hpp"

#include "Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Vulkan/RenderImage.hpp"

#include "Utilities/Math.hpp"

namespace Vulkan::ModernDeferred {

SoftwareTracingRenderer::SoftwareTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
	
}

SoftwareTracingRenderer::~SoftwareTracingRenderer()
{
	SoftwareTracingRenderer::DeleteSwapChain();
}

void SoftwareTracingRenderer::CreateSwapChain(const VkExtent2D& extent)
{
	rtPingPong0.reset(new RenderImage(Device(), extent,
									  VK_FORMAT_R16G16B16A16_SFLOAT,
									  VK_IMAGE_TILING_OPTIMAL,
									  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false,"prevoutput"));
	rtPingPong1.reset(new RenderImage(Device(), extent,
									  VK_FORMAT_R16G16B16A16_SFLOAT,
									  VK_IMAGE_TILING_OPTIMAL,
									  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false,"prevoutputspec"));

	rtPingPong3.reset(new RenderImage(Device(), extent,
									  VK_FORMAT_R16G16B16A16_SFLOAT,
									  VK_IMAGE_TILING_OPTIMAL,
									  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, false,"prevoutputalbedo"));

	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(), baseRender_, UniformBuffers(), GetScene()));
	accumulatePipeline_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(), baseRender_, baseRender_.rtOutputDiffuse->GetImageView(), rtPingPong0->GetImageView(), baseRender_.rtAccumlatedDiffuse->GetImageView(), UniformBuffers(), GetScene()));
	accumulatePipelineSpec_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(),baseRender_, baseRender_.rtOutputSpecular->GetImageView(), rtPingPong1->GetImageView(), baseRender_.rtAccumlatedSpecular->GetImageView(), UniformBuffers(), GetScene()));
	accumulatePipelineAlbedo_.reset(new PipelineCommon::AccumulatePipeline(SwapChain(), baseRender_, baseRender_.rtAlbedo_->GetImageView(), rtPingPong3->GetImageView(), baseRender_.rtAccumlatedAlbedo_->GetImageView(), UniformBuffers(), GetScene()));

	composePipeline_.reset(new PipelineCommon::FinalComposePipeline(SwapChain(), baseRender_, UniformBuffers()));
}

void SoftwareTracingRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
	accumulatePipeline_.reset();
	accumulatePipelineSpec_.reset();
	accumulatePipelineAlbedo_.reset();
	composePipeline_.reset();
	rtPingPong0.reset();
	rtPingPong1.reset();
	rtPingPong3.reset();
}

void SoftwareTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	baseRender_.InitializeBarriers(commandBuffer);
	rtPingPong0->InsertBarrier(commandBuffer, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
	
	{
		SCOPED_GPU_TIMER("shadingpass");
		// cs shading pass
		VkDescriptorSet DescriptorSets[] = {deferredShadingPipeline_->DescriptorSet(imageIndex)};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
		deferredShadingPipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);

		baseRender_.rtOutputDiffuse->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}

	{
		SCOPED_GPU_TIMER("reproject pass");
		VkDescriptorSet DescriptorSets[] = {accumulatePipeline_->DescriptorSet(imageIndex)};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipeline_->Handle());
		accumulatePipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
		vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

		baseRender_.rtAccumlatedDiffuse->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipelineSpec_->Handle());
		accumulatePipelineSpec_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
		vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

		baseRender_.rtAccumlatedSpecular->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, accumulatePipelineAlbedo_->Handle());
		accumulatePipelineAlbedo_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
		vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

		baseRender_.rtAccumlatedAlbedo_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);	}
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
		
		baseRender_.rtAccumlatedDiffuse->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		rtPingPong0->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        
		VkImageCopy copyRegion;
		copyRegion.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copyRegion.srcOffset = {0, 0, 0};
		copyRegion.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
		copyRegion.dstOffset = {0, 0, 0};
		copyRegion.extent = {rtPingPong0->GetImage().Extent().width, rtPingPong0->GetImage().Extent().height, 1};
        
		vkCmdCopyImage(commandBuffer, baseRender_.rtAccumlatedDiffuse->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtPingPong0->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		baseRender_.rtAccumlatedSpecular->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		rtPingPong1->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		vkCmdCopyImage(commandBuffer, baseRender_.rtAccumlatedSpecular->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtPingPong1->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

		baseRender_.rtAccumlatedAlbedo_->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
		rtPingPong3->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

		vkCmdCopyImage(commandBuffer, baseRender_.rtAccumlatedAlbedo_->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rtPingPong3->GetImage().Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);
	}
}
}
