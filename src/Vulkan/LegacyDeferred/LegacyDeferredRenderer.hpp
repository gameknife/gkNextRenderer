#pragma once

#include "Vulkan/FrameBuffer.hpp"
#include "Vulkan/WindowConfig.hpp"
#include "Vulkan/Image.hpp"
#include "Vulkan/VulkanBaseRenderer.hpp"
#include "Vulkan/RayTracing/RayTraceBaseRenderer.hpp"

#include <vector>
#include <memory>

#include "Vulkan/ModernDeferred/ModernDeferredPipeline.hpp"

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
	class LegacyDeferredRenderer final : public Vulkan::LogicRendererBase
	{
	public:

		VULKAN_NON_COPIABLE(LegacyDeferredRenderer)
		
		LegacyDeferredRenderer(Vulkan::VulkanBaseRenderer& baseRender);
		~LegacyDeferredRenderer();

		void CreateSwapChain(const VkExtent2D& extent) override;
		void DeleteSwapChain() override;
		void Render(VkCommandBuffer commandBuffer, uint32_t imageIndex) override;

	private:
		std::unique_ptr<Vulkan::ModernDeferred::VisibilityPipeline> visibilityPipeline0_;
		std::unique_ptr<class ShadingPipeline> deferredShadingPipeline_;
		std::unique_ptr<Vulkan::PipelineCommon::SimpleComposePipeline> composePipeline_;
		std::unique_ptr<class FrameBuffer> deferredFrameBuffer_;
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
		std::unique_ptr<class ShadingPipeline> deferredShadingPipeline_;
		std::unique_ptr<Vulkan::PipelineCommon::SimpleComposePipeline> composePipeline_;
	};

}
