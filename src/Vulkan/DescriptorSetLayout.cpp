#include "DescriptorSetLayout.hpp"
#include "Device.hpp"

namespace Vulkan {

DescriptorSetLayout::DescriptorSetLayout(const Device& device, const std::vector<DescriptorBinding>& descriptorBindings, bool bindless) :
	device_(device)
{
	std::vector<VkDescriptorSetLayoutBinding> layoutBindings;

	for (const auto& binding : descriptorBindings)
	{
		VkDescriptorSetLayoutBinding b = {};
		b.binding = binding.Binding;
		b.descriptorCount = binding.DescriptorCount;
		b.descriptorType = binding.Type;
		b.stageFlags = binding.Stage;

		layoutBindings.push_back(b);
	}

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
	layoutInfo.pBindings = layoutBindings.data();

	// bindless stuff
	VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_info{
		VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr
	};
	layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
	VkDescriptorBindingFlags bindless_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT |
		VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;
	extended_info.bindingCount = 1;
	extended_info.pBindingFlags = &bindless_flags;
	
	if( bindless ) layoutInfo.pNext = &extended_info;

	Check(vkCreateDescriptorSetLayout(device.Handle(), &layoutInfo, nullptr, &layout_),
		"create descriptor set layout");
}

DescriptorSetLayout::~DescriptorSetLayout()
{
	if (layout_ != nullptr)
	{
		vkDestroyDescriptorSetLayout(device_.Handle(), layout_, nullptr);
		layout_ = nullptr;
	}
}

}
