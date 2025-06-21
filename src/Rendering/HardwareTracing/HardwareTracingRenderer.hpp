#pragma once

#include "Vulkan/Image.hpp"
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
	class HardwareTracingRenderer final : public Vulkan::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(HardwareTracingRenderer)
		
		HardwareTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender);
		~HardwareTracingRenderer();
		
		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<class HardwareTracingPipeline> deferredShadingPipeline_;
		std::unique_ptr<PipelineCommon::AccumulatePipeline> accumulatePipeline_;
		std::unique_ptr<PipelineCommon::FinalComposePipeline> composePipeline_;
		std::unique_ptr<RenderImage> rtPingPong0;
	};

}