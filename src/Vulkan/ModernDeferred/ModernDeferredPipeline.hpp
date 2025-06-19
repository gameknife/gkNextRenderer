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

namespace Vulkan::ModernDeferred
{
	class ShadingPipeline final
	{
	public:
		VULKAN_NON_COPIABLE(ShadingPipeline)
	
		ShadingPipeline(
			const SwapChain& swapChain, 
			const VulkanBaseRenderer& baseRenderer,
			const std::vector<Assets::UniformBuffer>& uniformBuffers,
			const Assets::Scene& scene);
		~ShadingPipeline();

		VkDescriptorSet DescriptorSet(uint32_t index) const;
		const Vulkan::PipelineLayout& PipelineLayout() const { return *pipelineLayout_; }
	private:
		const SwapChain& swapChain_;
		
		VULKAN_HANDLE(VkPipeline, pipeline_)

		std::unique_ptr<Vulkan::DescriptorSetManager> descriptorSetManager_;
		std::unique_ptr<Vulkan::PipelineLayout> pipelineLayout_;
	};

}
