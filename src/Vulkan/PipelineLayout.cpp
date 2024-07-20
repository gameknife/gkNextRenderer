#include "PipelineLayout.hpp"
#include "DescriptorSetLayout.hpp"
#include "Device.hpp"
#include "Assets/Texture.hpp"

namespace Vulkan {

PipelineLayout::PipelineLayout(const Device & device, const DescriptorSetLayout& descriptorSetLayout, const VkPushConstantRange* pushConstantRanges, uint32_t pushConstantRangeCount) :
	device_(device)
{
	// add the global texture set with set = 1, currently an ugly impl
	Assets::GlobalTexturePool* GPool = Assets::GlobalTexturePool::GetInstance();
	VkDescriptorSetLayout descriptorSetLayouts[] = { descriptorSetLayout.Handle(), GPool->Layout() };

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1 + 1;
	pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts;
	pipelineLayoutInfo.pushConstantRangeCount = pushConstantRangeCount;
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges;
	
	Check(vkCreatePipelineLayout(device_.Handle(), &pipelineLayoutInfo, nullptr, &pipelineLayout_),
		"create pipeline layout");
}

PipelineLayout::~PipelineLayout()
{
	if (pipelineLayout_ != nullptr)
	{
		vkDestroyPipelineLayout(device_.Handle(), pipelineLayout_, nullptr);
		pipelineLayout_ = nullptr;
	}
}

}
