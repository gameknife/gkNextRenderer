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
	class Buffer;
	class DescriptorSetManager;
	class VulkanBaseRenderer;

	namespace RayTracing
	{
		class TopLevelAccelerationStructure;
	}
}



namespace Vulkan::HybridDeferred
{
	class HardwareTracingPipeline final
	{
	public:
		VULKAN_NON_COPIABLE(HardwareTracingPipeline)
	
		HardwareTracingPipeline(
			const SwapChain& swapChain, const RayTracing::TopLevelAccelerationStructure& accelerationStructure,
			const VulkanBaseRenderer& baseRenderer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~HardwareTracingPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }

	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

}
