#pragma once

#include "Engine/Vulkan/DebugUtilities.hpp"
#include "Engine/Vulkan/GpuResources.hpp"
#include "Engine/Vulkan/RenderingPipeline.hpp"

#include <memory>
#include <vector>

namespace Assets
{
	class Scene;
	class UniformBuffer;
}

namespace Vulkan
{
	class DepthBuffer;
	class PipelineLayout;
	class RenderPass;
	class SwapChain;
	class DescriptorSetManager;
	class DeviceProcedures;
	class Buffer;
}

namespace Vulkan::PipelineCommon
{
	class ZeroBindWithTLASPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(ZeroBindWithTLASPipeline)
		ZeroBindWithTLASPipeline(const SwapChain& swapChain, const char* shaderfile, const Assets::Scene& scene);

		void BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex);
	};
	
	class ZeroBindPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(ZeroBindPipeline)
		ZeroBindPipeline(const SwapChain& swapChain, const char* shaderfile, const Assets::Scene& scene);

		void BindPipeline(VkCommandBuffer commandBuffer, const Assets::Scene& scene, uint32_t imageIndex);
	};
	
	class ZeroBindCustomPushConstantPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(ZeroBindCustomPushConstantPipeline)
		ZeroBindCustomPushConstantPipeline(const SwapChain& swapChain, const char* shaderfile, uint32_t pushConstantSize);
		void BindPipeline(VkCommandBuffer commandBuffer, const void* data);

	private:
		uint32_t pushConstantSize_;
	};

	class VisibilityPipeline : public PipelineBase
	{
	public:
		VULKAN_NON_COPIABLE(VisibilityPipeline)

		VisibilityPipeline(
			const SwapChain& swapChain, 
			const DepthBuffer& depthBuffer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~VisibilityPipeline();
		
		const Vulkan::RenderPass& RenderPass() const { return *renderPass_; }

	private:
		std::unique_ptr<Vulkan::RenderPass> renderPass_;
		std::unique_ptr<Vulkan::RenderPass> swapRenderPass_;
	};

	class GraphicsPipeline : public PipelineBase
	{
	public:

		VULKAN_NON_COPIABLE(GraphicsPipeline)

		GraphicsPipeline(
			const SwapChain& swapChain, 
			const DepthBuffer& depthBuffer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene,
			bool isWireFrame);
		~GraphicsPipeline();
		
		const class RenderPass& RenderPass() const { return *renderPass_; }

	private:
		std::unique_ptr<class RenderPass> renderPass_;
	};
}
