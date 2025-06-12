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

		PipelineLayout(const Device& device, const DescriptorSetLayout& descriptorSetLayout0, const DescriptorSetLayout& descriptorSetLayout1, const DescriptorSetLayout& descriptorSetLayout2, const VkPushConstantRange* pushConstantRanges = nullptr, uint32_t pushConstantRangeCount = 0);
		PipelineLayout(const Device& device, const DescriptorSetLayout& descriptorSetLayout, const VkPushConstantRange* pushConstantRanges = nullptr, uint32_t pushConstantRangeCount = 0);
		~PipelineLayout();

	private:

		const Device& device_;

		VULKAN_HANDLE(VkPipelineLayout, pipelineLayout_)
	};

}
