#include "PipelineLayout.hpp"
#include "DescriptorSetLayout.hpp"
#include "Device.hpp"
#include "Assets/Texture.hpp"

namespace Vulkan {
	
PipelineLayout::PipelineLayout(const Device& device, const std::vector<DescriptorSetManager*> managers, uint32_t maxSets, const VkPushConstantRange* pushConstantRanges,
	uint32_t pushConstantRangeCount) : device_(device)
{
	for ( DescriptorSetManager* manager : managers )
	{
		cachedDescriptorSetLayouts_.push_back(manager->DescriptorSetLayout().Handle());
	}

	cachedDescriptorSets_.resize(maxSets);
	for( uint32_t i = 0; i < maxSets; ++i )
	{
		for ( DescriptorSetManager* manager : managers )
		{
			cachedDescriptorSets_[i].push_back(manager->DescriptorSets().Handle(i));
		}
	}
	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 4;
	pipelineLayoutInfo.pSetLayouts = cachedDescriptorSetLayouts_.data();
	pipelineLayoutInfo.pushConstantRangeCount = pushConstantRangeCount;
	pipelineLayoutInfo.pPushConstantRanges = pushConstantRanges;
	
	Check(vkCreatePipelineLayout(device_.Handle(), &pipelineLayoutInfo, nullptr, &pipelineLayout_),
		"create pipeline layout");
}

PipelineLayout::PipelineLayout(const Device & device, const DescriptorSetLayout& descriptorSetLayout, const VkPushConstantRange* pushConstantRanges, uint32_t pushConstantRangeCount) :
	device_(device)
{
	// add the global texture set with set = 1, currently an ugly impl
	Assets::GlobalTexturePool* GPool = Assets::GlobalTexturePool::GetInstance();
	cachedDescriptorSetLayouts_ = { descriptorSetLayout.Handle(), GPool->Layout() };

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 2;
	pipelineLayoutInfo.pSetLayouts = cachedDescriptorSetLayouts_.data();
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

void PipelineLayout::BindDescriptorSets(VkCommandBuffer commandBuffer, uint32_t idx) const
{
	vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,Handle(), 0,
						 static_cast<uint32_t>(cachedDescriptorSets_.size()), cachedDescriptorSets_[idx].data(), 0, nullptr );

}
}
