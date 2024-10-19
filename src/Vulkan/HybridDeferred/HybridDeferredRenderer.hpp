#pragma once

#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/WindowConfig.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/RayTracing/RayTraceBaseRenderer.hpp"
#include <vector>
#include <memory>


namespace Vulkan
{
	class RenderImage;
}

namespace Vulkan::PipelineCommon
{
	class AccumulatePipeline;
	class FinalComposePipeline;
}

namespace Vulkan::ModernDeferred
{
	class VisibilityPipeline;
}
namespace Assets
{
	class Scene;
	struct UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan::HybridDeferred
{
	class HybridDeferredRenderer final : public RayTracing::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(HybridDeferredRenderer)
		
		HybridDeferredRenderer(RayTracing::RayTraceBaseRenderer& baseRender);
		~HybridDeferredRenderer();
		
		void CreateSwapChain() override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<ModernDeferred::VisibilityPipeline> visibilityPipeline0_;
		std::unique_ptr<ModernDeferred::VisibilityPipeline> visibilityPipeline1_;
		
		std::unique_ptr<class HybridShadingPipeline> deferredShadingPipeline_;
		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulateForLightPipeline_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipeline_;
		std::unique_ptr<PipelineCommon::VisualDebuggerPipeline> visualDebugPipeline_;
		
		std::unique_ptr<class FrameBuffer> deferredFrameBuffer0_;
		std::unique_ptr<class FrameBuffer> deferredFrameBuffer1_;
		
		std::unique_ptr<RenderImage> rtAccumlation;

		std::unique_ptr<RenderImage> rtDirectLightSource;
		std::unique_ptr<RenderImage> rtDirectLight0;
		std::unique_ptr<RenderImage> rtDirectLight1;
		std::unique_ptr<RenderImage> rtDirectLightDest;

		std::unique_ptr<RenderImage> rtPingPong0;
		std::unique_ptr<RenderImage> rtPingPong1;

		std::unique_ptr<RenderImage> rtVisibility0;
		std::unique_ptr<RenderImage> rtVisibility1;
		
		std::unique_ptr<RenderImage> rtOutput;
		std::unique_ptr<RenderImage> rtMotionVector;

		std::unique_ptr<RenderImage> rtAlbedo_;
		std::unique_ptr<RenderImage> rtNormal_;
	};

}
