#pragma once

#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/WindowConfig.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"
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
}

namespace Assets
{
	class Scene;
	struct UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan::ModernDeferred
{
	class ModernDeferredRenderer final : public Vulkan::RayTracing::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(ModernDeferredRenderer)
		
		ModernDeferredRenderer(RayTracing::RayTraceBaseRenderer& baseRender);
		~ModernDeferredRenderer();
		
		void CreateSwapChain() override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<class VisibilityPipeline> visibilityPipeline_;
		std::unique_ptr<class ShadingPipeline> deferredShadingPipeline_;
		std::unique_ptr<class FrameBuffer> deferredFrameBuffer_;

		std::unique_ptr<RenderImage> rtVisibilityBuffer_;
		std::unique_ptr<RenderImage> rtOutput_;
		std::unique_ptr<RenderImage> rtMotionVector_;
	};

}
