#include "Engine/Rendering/SoftwareTracing/SoftwareTracingRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Runtime/Engine.hpp"

#include "Engine/Vulkan/SwapChain.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

#include "Engine/Utilities/Math.hpp"

namespace Vulkan::SoftwareTracing {

SoftwareTracingRenderer::SoftwareTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender):LogicRendererBase(baseRender)
{
	
}

SoftwareTracingRenderer::~SoftwareTracingRenderer()
{
	SoftwareTracingRenderer::DeleteSwapChain();
}

void SoftwareTracingRenderer::CreateSwapChain(const VkExtent2D& extent)
{
	deferredShadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Core.SwTracing.comp.slang.spv", GetScene()));
	accumulatePipeline_.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(SwapChain(), "assets/shaders/Process.ReProject.comp.slang.spv", 24));
	atrousDenoiser_.CreateSwapChain(SwapChain());
	composePipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Process.DenoiseJBF.comp.slang.spv", GetScene()));

	temporalResolve_.SetupHistory(baseRender_, {
		{PipelineCommon::ETemporalChannel::Diffuse, Assets::Bindless::RT_SINGLE_PREV_DIFFUSE, "prevDiffuseTmp"},
		{PipelineCommon::ETemporalChannel::Specular, Assets::Bindless::RT_SINGLE_PREV_SPECULAR, "prevSpecularTmp"},
		{PipelineCommon::ETemporalChannel::Albedo, Assets::Bindless::RT_SINGLE_PREV_ALBEDO, "prevAlbedoTmp"},
	});
}

void SoftwareTracingRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
	accumulatePipeline_.reset();
	atrousDenoiser_.DeleteSwapChain();
	composePipeline_.reset();
}

void SoftwareTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	baseRender_.InitializeBarriers(commandBuffer);
	{
		SCOPED_GPU_TIMER("shadingpass");
		// cs shading pass
		deferredShadingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
		vkCmdDispatch(commandBuffer,
			Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
			Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

		baseRender_.GetStorageImage(Assets::Bindless::RT_SINGLE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}

	{
		SCOPED_GPU_TIMER("reproject pass");
		std::array<uint32_t, 6> pushConst { NextEngine::GetInstance()->IsProgressiveRendering(), uint32_t(NextEngine::GetInstance()->GetUserSettings().TemporalFrames),
					   temporalResolve_.History(PipelineCommon::ETemporalChannel::Diffuse),
					   temporalResolve_.History(PipelineCommon::ETemporalChannel::Specular),
					   temporalResolve_.History(PipelineCommon::ETemporalChannel::Albedo), 1 };
		accumulatePipeline_->BindPipeline(commandBuffer, pushConst.data());
		vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8), Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);

		baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.GetStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}
	{
		SCOPED_GPU_TIMER("atrous pass");
		atrousDenoiser_.Run(baseRender_, SwapChain(), commandBuffer, NextEngine::GetInstance()->GetUserSettings());
	}
	{
		SCOPED_GPU_TIMER("compose pass");

		composePipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
		vkCmdDispatch(commandBuffer,
			Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().width, 8),
			Utilities::Math::GetSafeDispatchCount(SwapChain().RenderExtent().height, 8), 1);
	}
	
	{
        SCOPED_GPU_TIMER("copy pass");
        temporalResolve_.CopyToHistory(baseRender_, commandBuffer, {
            {Assets::Bindless::RT_ACCUMLATE_DIFFUSE, PipelineCommon::ETemporalChannel::Diffuse},
            {Assets::Bindless::RT_ACCUMLATE_SPECULAR, PipelineCommon::ETemporalChannel::Specular},
            {Assets::Bindless::RT_ACCUMLATE_ALBEDO, PipelineCommon::ETemporalChannel::Albedo},
        });
    }
}
}
