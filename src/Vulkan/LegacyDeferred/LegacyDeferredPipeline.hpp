#pragma once

#include "Vulkan/Vulkan.hpp"
#include "Vulkan/ImageView.hpp"
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
}

namespace Vulkan::LegacyDeferred
{

	class GBufferPipeline final
	{
	public:

		VULKAN_NON_COPIABLE(GBufferPipeline)

		GBufferPipeline(
			const SwapChain& swapChain, 
			const DepthBuffer& depthBuffer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~GBufferPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
		const RenderPass& RenderPass() const { return *renderPass_; }

	private:
		const SwapChain& swapChain_;

		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<class PipelineLayout> pipelineLayout_;
		std::unique_ptr<class RenderPass> renderPass_;
		std::unique_ptr<class RenderPass> swapRenderPass_;
	};

	class ShadingPipeline final
	{
	public:
		VULKAN_NON_COPIABLE(ShadingPipeline)
	
		ShadingPipeline(
			const SwapChain& swapChain, 
			const ImageView& gbuffer0ImageView,
			const ImageView& gbuffer1ImageView,
			const ImageView& gbuffer2ImageView,
			const ImageView& finalImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~ShadingPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<class PipelineLayout> pipelineLayout_;
	};

}
