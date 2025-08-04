#pragma once

#include "Vulkan.hpp"
#include <vector>

namespace Vulkan
{
	class DescriptorSetLayout;
	class Device;
	class DescriptorSetManager;

	class PipelineLayout final
	{
	public:

		VULKAN_NON_COPIABLE(PipelineLayout)

		PipelineLayout(const Device& device, const std::vector<DescriptorSetManager*> managers, uint32_t maxSets, const VkPushConstantRange* pushConstantRanges = nullptr, uint32_t pushConstantRangeCount = 0);
		PipelineLayout(const Device& device, const DescriptorSetLayout& descriptorSetLayout, const VkPushConstantRange* pushConstantRanges = nullptr, uint32_t pushConstantRangeCount = 0);
		~PipelineLayout();

		void BindDescriptorSets(VkCommandBuffer commandBuffer, uint32_t idx) const;
	private:

		const Device& device_;

		VULKAN_HANDLE(VkPipelineLayout, pipelineLayout_)

		std::vector<VkDescriptorSetLayout> cachedDescriptorSetLayouts_;
		std::vector< std::vector<VkDescriptorSet> > cachedDescriptorSets_;
	};

}
