#include "Engine/Rendering/SoftwareTracing/SoftwareTracingRenderer.hpp"
#include "Engine/Rendering/PipelineCommon/CommonComputePipeline.hpp"
#include "Engine/Runtime/Engine.hpp"
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
	temporalPostChain_.CreateSwapChain(SwapChain(), GetScene());
}

void SoftwareTracingRenderer::DeleteSwapChain()
{
	deferredShadingPipeline_.reset();
	temporalPostChain_.DeleteSwapChain();
}

void SoftwareTracingRenderer::ReloadShaders(
	const std::set<std::string>& changedShaderFiles,
	std::set<std::string>& handledShaderFiles)
{
	if (deferredShadingPipeline_) deferredShadingPipeline_->ReloadIfShaderChanged(changedShaderFiles, handledShaderFiles);
	temporalPostChain_.ReloadShaders(changedShaderFiles, handledShaderFiles);
}

void SoftwareTracingRenderer::Render(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
	baseRender_.ActiveRenderView().TemporalResolve().PrepareHistoryForRead(baseRender_, commandBuffer);
	baseRender_.ImportActiveViewImagesGeneral({
		Assets::Bindless::RT_SINGLE_DIFFUSE, Assets::Bindless::RT_SINGLE_SPECULAR,
		Assets::Bindless::RT_ALBEDO, Assets::Bindless::RT_NORMAL,
		Assets::Bindless::RT_OBJEDCTID_0, Assets::Bindless::RT_OBJEDCTID_1,
		Assets::Bindless::RT_PREV_DEPTHBUFFER,
		Assets::Bindless::RT_MOTIONVECTOR, Assets::Bindless::RT_DIFFUSE_HITDIST,
		Assets::Bindless::RT_SPECULAR_HITDIST, Assets::Bindless::RT_SPECULAR_ALBEDO,
		Assets::Bindless::RT_ACCUMLATE_DIFFUSE, Assets::Bindless::RT_ACCUMLATE_SPECULAR,
		Assets::Bindless::RT_ACCUMLATE_ALBEDO, Assets::Bindless::RT_MOTIONMOMENT,
		Assets::Bindless::RT_DENOISED,
	}, "scene image initialization");
	const bool isPrimaryView = baseRender_.ActiveViewBankBase() == 0;
	const bool allowTemporal = baseRender_.ActiveRenderView().Schedule() != EViewSchedule::Transient;
	const VkExtent2D activeExtent = baseRender_.ActiveViewRenderExtent();
	{
		SCOPED_GPU_TIMER("shadingpass");
		baseRender_.TransitionActiveViewImages(commandBuffer, {
			{Assets::Bindless::RT_SINGLE_DIFFUSE, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
			{Assets::Bindless::RT_SINGLE_SPECULAR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
			{Assets::Bindless::RT_ALBEDO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
			{Assets::Bindless::RT_NORMAL, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
			{Assets::Bindless::RT_OBJEDCTID_0, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
			{Assets::Bindless::RT_PREV_DEPTHBUFFER, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
			{Assets::Bindless::RT_MOTIONVECTOR, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
			{Assets::Bindless::RT_DIFFUSE_HITDIST, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
			{Assets::Bindless::RT_SPECULAR_HITDIST, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
			{Assets::Bindless::RT_SPECULAR_ALBEDO, PipelineCommon::ERenderStage::Compute, PipelineCommon::EResourceAccess::ShaderWrite},
		}, "software tracing shading");
		// cs shading pass
		deferredShadingPipeline_->BindPipeline(commandBuffer,
			GetScene().FetchGPUScene(imageIndex, baseRender_.ActiveViewBankBase()));
		vkCmdDispatch(commandBuffer,
			Utilities::Math::GetSafeDispatchCount(activeExtent.width, 8),
			Utilities::Math::GetSafeDispatchCount(activeExtent.height, 8), 1);
	}
	const auto& settings = NextEngine::GetInstance()->GetUserSettings();
	temporalPostChain_.Run(baseRender_, SwapChain(), commandBuffer, imageIndex, settings, {
		.progressiveRender = isPrimaryView && NextEngine::GetInstance()->IsProgressiveRendering(),
		.fastReproject = true,
		.runAtrous = true,
		.temporalFrames = allowTemporal ? uint32_t(settings.TemporalFrames) : 1u,
	});
}
}
