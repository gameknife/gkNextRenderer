#pragma once

#include "Vulkan.hpp"

namespace Vulkan
{
	class DescriptorSetLayout;
	class Device;

	class PipelineLayout final
	{
	public:

		VULKAN_NON_COPIABLE(PipelineLayout)

		PipelineLayout(const Device& device, const std::vector<DescriptorSetManager*> managers, const VkPushConstantRange* pushConstantRanges = nullptr, uint32_t pushConstantRangeCount = 0);
		PipelineLayout(const Device& device, const DescriptorSetLayout& descriptorSetLayout, const VkPushConstantRange* pushConstantRanges = nullptr, uint32_t pushConstantRangeCount = 0);
		~PipelineLayout();

		void BindDescriptorSets(VkCommandBuffer commandBuffer) const;
	private:

		const Device& device_;

		VULKAN_HANDLE(VkPipelineLayout, pipelineLayout_)

		std::vector<VkDescriptorSetLayout> cachedDescriptorSetLayouts_;
		std::vector<VkDescriptorSet> cachedDescriptorSets_;
	};

}
