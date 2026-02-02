#pragma once

#include "Vulkan/RenderingPipeline.hpp"
#include "Rendering/VulkanBaseRenderer.hpp"
#include "Rendering/RayTraceBaseRenderer.hpp"

#include <memory>

namespace Vulkan
{
	class RenderImage;
}

namespace Assets
{
	class Scene;
	struct UniformBufferObject;
	class UniformBuffer;
}

namespace Vulkan::LegacyDeferred
{
	class SoftwareModernRenderer final : public Vulkan::LogicRendererBase
	{
	public:
		VULKAN_NON_COPIABLE(SoftwareModernRenderer)
		
		SoftwareModernRenderer(Vulkan::VulkanBaseRenderer& baseRender);
		~SoftwareModernRenderer();

		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> deferredShadingPipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindPipeline> composePipeline_;
		std::unique_ptr<PipelineCommon::ZeroBindCustomPushConstantPipeline> accumulatePipeline_;
		
		uint32_t prevSingleDiffuseId_{};
		uint32_t prevSingleSpecularId_{};
		uint32_t prevSingleAlbedoId_{};
	};

}

namespace Vulkan::VoxelTracing
{
	class VoxelTracingRenderer final : public Vulkan::LogicRendererBase
	{
	public:
		VULKAN_NON_COPIABLE(VoxelTracingRenderer)
		
		VoxelTracingRenderer(Vulkan::VulkanBaseRenderer& baseRender);
		~VoxelTracingRenderer();

		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		// just one computer pass is enough
		std::unique_ptr<Vulkan::PipelineCommon::ZeroBindPipeline> deferredShadingPipeline_;
	};

}
