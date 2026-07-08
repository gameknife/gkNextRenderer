#include "Engine/Rendering/SoftwareModern/SoftwareModernRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Runtime/Engine.hpp"
#include "Engine/Utilities/Math.hpp"
#include "Engine/Vulkan/GpuResources.hpp"

#include <utility>

namespace Vulkan::SoftwareModern {

namespace
{
	struct FReprojectPushConstants
	{
		uint32_t ProgressiveRender;
		uint32_t TemporalFrames;
		uint32_t FastReproject;
		float ClampGammaHi;
		float ClampGammaLo;
		float ClampFloor;
	};
}

SoftwareModernRenderer::SoftwareModernRenderer(Vulkan::VulkanBaseRenderer& baseRender) :
	LogicRendererBase(baseRender)
{

}

SoftwareModernRenderer::~SoftwareModernRenderer()
{
	DeleteSwapChain();
}

void SoftwareModernRenderer::CreateSwapChain(const VkExtent2D& extent)
{
	(void)extent;
	deferredShadingPipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Core.SwModern.comp.slang.spv", GetScene()));

	accumulatePipeline_.reset(new PipelineCommon::ZeroBindCustomPushConstantPipeline(
		SwapChain(), "assets/shaders/Process.ReProject.comp.slang.spv", sizeof(FReprojectPushConstants)));
	composePipeline_.reset(new PipelineCommon::ZeroBindPipeline(SwapChain(), "assets/shaders/Process.DenoiseJBF.comp.slang.spv", GetScene()));
}
	
void SoftwareModernRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
	accumulatePipeline_.reset();
	composePipeline_.reset();
}

void SoftwareModernRenderer::ReloadShaders(
	const std::set<std::string>& changedShaderFiles,
	std::set<std::string>& handledShaderFiles)
{
	if (deferredShadingPipeline_)
	{
		deferredShadingPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
	}
	if (accumulatePipeline_)
	{
		accumulatePipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
	}
	if (composePipeline_)
	{
		composePipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
	}
}

void SoftwareModernRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	baseRender_.InitializeBarriers(commandBuffer);
	const bool isPrimaryView = baseRender_.ActiveViewBankBase() == 0;
	const bool allowTemporal = baseRender_.ActiveRenderView().Schedule() != EViewSchedule::Transient;
	const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();
	
	{
		SCOPED_GPU_TIMER("shadingpass");
		deferredShadingPipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
		vkCmdDispatch(commandBuffer,
			Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
			Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
	    
		const auto transitionShadingOutput = [this, commandBuffer](uint32_t bindlessId)
		{
			baseRender_.GetViewStorageImage(bindlessId)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT,
				VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
		};
		transitionShadingOutput(Assets::Bindless::RT_SINGLE_DIFFUSE);
		transitionShadingOutput(Assets::Bindless::RT_ALBEDO);
		transitionShadingOutput(Assets::Bindless::RT_OBJEDCTID_0);
		transitionShadingOutput(Assets::Bindless::RT_PREV_DEPTHBUFFER);
		transitionShadingOutput(Assets::Bindless::RT_SINGLE_SPECULAR);
		transitionShadingOutput(Assets::Bindless::RT_NORMAL);
		transitionShadingOutput(Assets::Bindless::RT_MOTIONVECTOR);
		transitionShadingOutput(Assets::Bindless::RT_DIFFUSE_HITDIST);
		transitionShadingOutput(Assets::Bindless::RT_SPECULAR_HITDIST);
		transitionShadingOutput(Assets::Bindless::RT_SPECULAR_ALBEDO);
		baseRender_.GetViewStorageImage(Assets::Bindless::RT_DENOISED)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT,
			VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}
	{
		SCOPED_GPU_TIMER("reproject pass");
		const auto& settings = NextEngine::GetInstance()->GetUserSettings();
		FReprojectPushConstants pushConst{};
		pushConst.ProgressiveRender = isPrimaryView && NextEngine::GetInstance()->IsProgressiveRendering();
		pushConst.TemporalFrames = allowTemporal ? uint32_t(settings.TemporalFrames) : 1u;
		pushConst.FastReproject = 1;
		pushConst.ClampGammaHi = settings.ReprojectClampGammaHi;
		pushConst.ClampGammaLo = settings.ReprojectClampGammaLo;
		pushConst.ClampFloor = settings.ReprojectClampFloor;
		accumulatePipeline_->BindPipeline(commandBuffer, &pushConst);
		vkCmdDispatch(commandBuffer, Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8), Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);

		baseRender_.GetViewStorageImage(Assets::Bindless::RT_ACCUMLATE_DIFFUSE)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.GetViewStorageImage(Assets::Bindless::RT_ACCUMLATE_SPECULAR)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
		baseRender_.GetViewStorageImage(Assets::Bindless::RT_ACCUMLATE_ALBEDO)->InsertBarrier(commandBuffer, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
	}
	{
		SCOPED_GPU_TIMER("atrous pass");
		baseRender_.ActiveRenderView().AtrousDenoiser().Run(
			baseRender_, SwapChain(), commandBuffer, NextEngine::GetInstance()->GetUserSettings());
	}
	{
		SCOPED_GPU_TIMER("compose pass");
		composePipeline_->BindPipeline(commandBuffer, GetScene(), imageIndex);
		vkCmdDispatch(commandBuffer,
			Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
			Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
	}
	{
        SCOPED_GPU_TIMER("copy pass");
        baseRender_.ActiveRenderView().TemporalResolve().CopyToHistory(baseRender_, commandBuffer, {
            {Assets::Bindless::RT_ACCUMLATE_DIFFUSE, PipelineCommon::ETemporalChannel::Diffuse},
            {Assets::Bindless::RT_ACCUMLATE_SPECULAR, PipelineCommon::ETemporalChannel::Specular},
            {Assets::Bindless::RT_ACCUMLATE_ALBEDO, PipelineCommon::ETemporalChannel::Albedo},
        });
    }
}
}
