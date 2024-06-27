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

namespace Vulkan::PipelineCommon
{
	class AccumulatePipeline final
	{
	public:
		VULKAN_NON_COPIABLE(AccumulatePipeline)
	
		AccumulatePipeline(
			const SwapChain& swapChain, 
			const ImageView& sourceImageView, const ImageView& accumulateImageView, const ImageView& prevAccumulateImageView, const ImageView& motionVectorImageView,
			const ImageView& visibilityBufferImageView,const ImageView& prevVisibilityBufferImageView,
			const ImageView& outputImage1View,
			const ImageView& adaptiveSampleImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~AccumulatePipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

	class FinalComposePipeline final
	{
	public:
		VULKAN_NON_COPIABLE(FinalComposePipeline)
	
		FinalComposePipeline(
			const SwapChain& swapChain, 
			const ImageView& sourceImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers);
		~FinalComposePipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};
}
