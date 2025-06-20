#include "LegacyDeferredRenderer.hpp"
#include "LegacyDeferredPipeline.hpp"

#include "Vulkan/PipelineLayout.hpp"
#include "Vulkan/RenderPass.hpp"
#include "Vulkan/SwapChain.hpp"
#include "Vulkan/Window.hpp"
#include "Vulkan/RenderImage.hpp"
#include "Assets/Model.hpp"
#include "Assets/Scene.hpp"
#include "Utilities/Exception.hpp"

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
	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(), baseRender_, UniformBuffers(), GetScene()));
	composePipeline_.reset(new Vulkan::PipelineCommon::SimpleComposePipeline(SwapChain(), baseRender_.rtOutput->GetImageView(), UniformBuffers()));
}
	
void LegacyDeferredRenderer::DeleteSwapChain()
{
	composePipeline_.reset();
	deferredShadingPipeline_.reset();
}

void LegacyDeferredRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	baseRender_.InitializeBarriers(commandBuffer);
	
	{
		SCOPED_GPU_TIMER("shadingpass");
		
		// cs shading pass
		VkDescriptorSet denoiserDescriptorSets[] = {deferredShadingPipeline_->DescriptorSet(imageIndex)};
		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
		deferredShadingPipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
		
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);	

		// copy to swap-buffer
		baseRender_.rtDenoised->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_GENERAL);
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
	deferredShadingPipeline_.reset(new ShadingPipeline(SwapChain(), baseRender_, UniformBuffers(), GetScene()));
	composePipeline_.reset(new Vulkan::PipelineCommon::SimpleComposePipeline(SwapChain(), baseRender_.rtOutput->GetImageView(), UniformBuffers()));
}

void Vulkan::VoxelTracing::VoxelTracingRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
	composePipeline_.reset();
}

void Vulkan::VoxelTracing::VoxelTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	baseRender_.InitializeBarriers(commandBuffer);
	
	{
		SCOPED_GPU_TIMER("shadingpass");

		vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, deferredShadingPipeline_->Handle());
		deferredShadingPipeline_->PipelineLayout().BindDescriptorSets(commandBuffer, imageIndex);
		vkCmdDispatch(commandBuffer, SwapChain().RenderExtent().width / 8, SwapChain().RenderExtent().height / 8, 1);	

		// copy to swap-buffer
		baseRender_.rtDenoised->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL,
			VK_IMAGE_LAYOUT_GENERAL);
	}
}
