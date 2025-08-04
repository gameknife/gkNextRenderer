#pragma once

#include "Vulkan/Vulkan.hpp"
#include <memory>
#include <vector>

namespace Assets
{
	class Scene;
	class UniformBuffer;
}

namespace Vulkan
{
	class DescriptorSetManager;
	class ImageView;
	class PipelineLayout;
	class SwapChain;
	class DeviceProcedures;
	class VulkanBaseRenderer;
}

namespace Vulkan::RayTracing
{
	
	class TopLevelAccelerationStructure;
	
	class PathTracingPipeline final
	{
	public:

		VULKAN_NON_COPIABLE(PathTracingPipeline)

		PathTracingPipeline(
			const SwapChain& swapChain,
			const TopLevelAccelerationStructure& accelerationStructure,
			const VulkanBaseRenderer& baseRenderer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~PathTracingPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const class PipelineLayout& PipelineLayout() const { return *PipelineLayout_; }
	private:

		const SwapChain& swapChain_;

		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<class PipelineLayout> PipelineLayout_;
	};
	
}
