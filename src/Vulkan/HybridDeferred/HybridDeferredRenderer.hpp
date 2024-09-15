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
	class UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan::HybridDeferred
{
	class HybridDeferredRenderer : public RayTracing::RayTraceBaseRenderer
	{
	public:

		VULKAN_NON_COPIABLE(HybridDeferredRenderer)
		
		HybridDeferredRenderer(const char* rendererType, Vulkan::Window* window, VkPresentModeKHR presentMode, bool enableValidationLayers);
		~HybridDeferredRenderer();
		
		void CreateSwapChain() override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<ModernDeferred::VisibilityPipeline> visibilityPipeline_;
		std::unique_ptr<ModernDeferred::VisibilityPipeline> visibility1Pipeline_;
		std::unique_ptr<class HybridShadingPipeline> deferredShadingPipeline_;
		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipeline_;
		std::unique_ptr<class FrameBuffer> deferredFrameBuffer_;
		std::unique_ptr<class FrameBuffer> deferred1FrameBuffer_;

		std::unique_ptr<RenderImage> rtAccumlation;
		std::unique_ptr<RenderImage> rtDirectLight0;
		std::unique_ptr<RenderImage> rtDirectLight1;

		std::unique_ptr<RenderImage> rtPingPong0;
		std::unique_ptr<RenderImage> rtPingPong1;

		std::unique_ptr<RenderImage> rtVisibility0;
		std::unique_ptr<RenderImage> rtVisibility1;
		
		std::unique_ptr<RenderImage> rtOutput;
		std::unique_ptr<RenderImage> rtMotionVector;

		std::unique_ptr<RenderImage> rtAdaptiveSample_;
	};

}
