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

	namespace RayTracing
	{
		class TopLevelAccelerationStructure;
	}
}



namespace Vulkan::HybridDeferred
{
	class HybridShadingPipeline final
	{
	public:
		VULKAN_NON_COPIABLE(HybridShadingPipeline)
	
		HybridShadingPipeline(
			const SwapChain& swapChain, const RayTracing::TopLevelAccelerationStructure& accelerationStructure,
			const ImageView& miniGBufferImageView, const ImageView& finalImageView, const ImageView& motionVectorImageView,
			const ImageView& directLight0ImageView, const ImageView& directLight1ImageView,
			const ImageView& albedoImageView, const ImageView& normalImageView,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~HybridShadingPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

}
