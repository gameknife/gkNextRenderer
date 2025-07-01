#pragma once

#include "Rendering/VulkanBaseRenderer.hpp"
#include "Rendering/RayTraceBaseRenderer.hpp"

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
	class SoftwareTracingRenderer final : public Vulkan::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(SoftwareTracingRenderer)
		
		SoftwareTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender);
		~SoftwareTracingRenderer();
		
		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<class ShadingPipeline> deferredShadingPipeline_;
		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulatePipelineSpec_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipeline_;
		std::unique_ptr<RenderImage> rtPingPong0;
		std::unique_ptr<RenderImage> rtPingPong1;
	};

}
